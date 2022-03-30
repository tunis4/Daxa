#pragma once

#include "chunk.hpp"
#include <Daxa.hpp>
#include <deque>

struct RenderContext {
    daxa::DeviceHandle device;
    daxa::PipelineCompilerHandle pipeline_compiler;
    daxa::CommandQueueHandle queue;

    daxa::SwapchainHandle swapchain;
    daxa::SwapchainImage swapchain_image;
    daxa::ImageViewHandle render_color_image, render_depth_image;

    struct PerFrameData {
        daxa::SignalHandle present_signal;
        daxa::TimelineSemaphoreHandle timeline;
        u64 timeline_counter = 0;
    };
    std::deque<PerFrameData> frames;

    auto create_color_image(glm::ivec2 dim) {
        auto result = device->createImageView({
            .image = device->createImage({
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .extent = {static_cast<u32>(dim.x), static_cast<u32>(dim.y), 1},
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .debugName = "Render Image",
            }),
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .debugName = "Render Image View",
        });
        return result;
    }

    auto create_depth_image(glm::ivec2 dim) {
        auto result = device->createImageView({
            .image = device->createImage({
                .format = VK_FORMAT_D32_SFLOAT,
                .extent = {static_cast<u32>(dim.x), static_cast<u32>(dim.y), 1},
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .debugName = "Depth Image",
            }),
            .format = VK_FORMAT_D32_SFLOAT,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .debugName = "Depth Image View",
        });
        return result;
    }

    RenderContext(VkSurfaceKHR surface, glm::ivec2 dim)
        : device(daxa::Device::create()),
          pipeline_compiler{this->device->createPipelineCompiler()},
          queue(device->createCommandQueue({.batchCount = 2})),
          swapchain(device->createSwapchain({
              .surface = surface,
              .width = static_cast<u32>(dim.x),
              .height = static_cast<u32>(dim.y),
              .presentMode = VK_PRESENT_MODE_FIFO_KHR,
              .additionalUses = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
              .debugName = "Swapchain",
          })),
          swapchain_image(swapchain->aquireNextImage()),
          render_color_image(create_color_image(dim)),
          render_depth_image(create_depth_image(dim)) {
        for (int i = 0; i < 3; i++) {
            frames.push_back(PerFrameData{
                .present_signal = device->createSignal({}),
                .timeline = device->createTimelineSemaphore({}),
                .timeline_counter = 0,
            });
        }
        pipeline_compiler->addShaderSourceRootPath("DaxaMinecraft/assets/shaders");
    }

    ~RenderContext() {
        queue->waitIdle();
        queue->checkForFinishedSubmits();
        device->waitIdle();
        frames.clear();
    }

    auto begin_frame(glm::ivec2 dim) {
        resize(dim);
        // auto *currentFrame = &frames.front();
        auto cmd_list = queue->getCommandList({});
        cmd_list->queueImageBarrier(daxa::ImageBarrier{
            .barrier = daxa::FULL_MEMORY_BARRIER,
            .image = render_color_image,
            .layoutBefore = VK_IMAGE_LAYOUT_UNDEFINED,
            .layoutAfter = VK_IMAGE_LAYOUT_GENERAL,
        });
        cmd_list->queueImageBarrier(daxa::ImageBarrier{
            .barrier = daxa::FULL_MEMORY_BARRIER,
            .image = render_depth_image,
            .layoutBefore = VK_IMAGE_LAYOUT_UNDEFINED,
            .layoutAfter = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        });
        return cmd_list;
    }

    void begin_rendering(daxa::CommandListHandle cmd_list) {
        std::array framebuffer{daxa::RenderAttachmentInfo{
            .image = swapchain_image.getImageViewHandle(),
            .clearValue = {.color = {.float32 = {1.0f, 0.0f, 1.0f, 1.0f}}},
        }};
        daxa::RenderAttachmentInfo depth_attachment{
            .image = render_depth_image,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .clearValue = {.depthStencil = {.depth = 1.0f}},
        };
        cmd_list->beginRendering(daxa::BeginRenderingInfo{
            .colorAttachments = framebuffer,
            .depthAttachment = &depth_attachment,
        });
    }

    void end_rendering(daxa::CommandListHandle cmd_list) {
        cmd_list->endRendering();
    }

    void end_frame(daxa::CommandListHandle cmd_list) {
        auto *current_frame = &frames.front();
        cmd_list->finalize();
        // std::array signalTimelines = {
        //     std::tuple{current_frame->timeline, ++current_frame->timeline_counter},
        // };
        daxa::SubmitInfo submitInfo;
        submitInfo.commandLists.push_back(std::move(cmd_list));
        submitInfo.signalOnCompletion = {&current_frame->present_signal, 1};
        // submitInfo.signalTimelines = signalTimelines;
        queue->submit(submitInfo);
        queue->present(std::move(swapchain_image), current_frame->present_signal);
        swapchain_image = swapchain->aquireNextImage();
        auto frameContext = std::move(frames.back());
        frames.pop_back();
        frames.push_front(std::move(frameContext));
        current_frame = &frames.front();
        queue->checkForFinishedSubmits();
        queue->nextBatch();
        // current_frame->timeline->wait(current_frame->timeline_counter);
    }

    void resize(glm::ivec2 dim) {
        if (dim.x != static_cast<i32>(swapchain->getSize().width) || dim.y != static_cast<i32>(swapchain->getSize().height)) {
            device->waitIdle();
            swapchain->resize(VkExtent2D{.width = static_cast<u32>(dim.x), .height = static_cast<u32>(dim.y)});
            swapchain_image = swapchain->aquireNextImage();
            render_color_image = create_color_image(dim);
            render_depth_image = create_depth_image(dim);
        }
    }

    void blit_to_swapchain(daxa::CommandListHandle cmd_list) {

        cmd_list->queueImageBarrier({
            .image = swapchain_image.getImageViewHandle(),
            .layoutBefore = VK_IMAGE_LAYOUT_UNDEFINED,
            .layoutAfter = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        });
        cmd_list->queueImageBarrier({
            .image = render_color_image,
            .layoutBefore = VK_IMAGE_LAYOUT_GENERAL,
            .layoutAfter = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        });

        // cmd_list->singleCopyImageToImage({
        //     .src = render_color_image->getImageHandle(),
        //     .dst = swapchain_image.getImageViewHandle()->getImageHandle(),
        // });

        auto render_extent = swapchain_image.getImageViewHandle()->getImageHandle()->getVkExtent3D();
        auto swap_extent = swapchain_image.getImageViewHandle()->getImageHandle()->getVkExtent3D();
        VkImageBlit blit{
            .srcSubresource = VkImageSubresourceLayers{
                .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcOffsets = {
                VkOffset3D{0, 0, 0},
                VkOffset3D{
                    static_cast<int32_t>(render_extent.width),
                    static_cast<int32_t>(render_extent.height),
                    static_cast<int32_t>(render_extent.depth),
                },
            },
            .dstSubresource = VkImageSubresourceLayers{
                .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .dstOffsets = {
                VkOffset3D{0, 0, 0},
                VkOffset3D{
                    static_cast<int32_t>(swap_extent.width),
                    static_cast<int32_t>(swap_extent.height),
                    static_cast<int32_t>(swap_extent.depth),
                },
            },
        };
        cmd_list->insertQueuedBarriers();
        vkCmdBlitImage(
            cmd_list->getVkCommandBuffer(),
            render_color_image->getImageHandle()->getVkImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swapchain_image.getImageViewHandle()->getImageHandle()->getVkImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        cmd_list->queueImageBarrier({
            .image = swapchain_image.getImageViewHandle(),
            .layoutBefore = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .layoutAfter = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        });
        cmd_list->queueImageBarrier({
            .image = render_color_image,
            .layoutBefore = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .layoutAfter = VK_IMAGE_LAYOUT_GENERAL,
        });
    }
};

struct RenderableChunk {
    daxa::ImageViewHandle chunkgen_image_a;
    daxa::ImageViewHandle chunkgen_image_b;
};

struct World {
    static constexpr glm::ivec3 DIM{8, 2, 8};

    template <typename T>
    using ChunkArray = std::array<std::array<std::array<T, DIM.x>, DIM.y>, DIM.z>;
    ChunkArray<std::unique_ptr<RenderableChunk>> chunks{};

    daxa::ImageViewHandle atlas_texture_array;

    daxa::BufferHandle compute_pipeline_globals;

    daxa::PipelineHandle raymarch_compute_pipeline;

    daxa::PipelineHandle chunkgen_compute_pipeline_pass0;
    daxa::PipelineHandle chunkgen_compute_pipeline_pass1;
    daxa::PipelineHandle sdf_compute_pipeline_passes[3];

    struct ComputeGlobals {
        glm::mat4 viewproj_mat;
        glm::vec4 pos;
        glm::ivec2 frame_dim;
        float time;

        u32 texture_index;
        ChunkArray<u32> chunk_ids[2];
    };

    struct ChunkgenPush {
        glm::vec4 pos;
        u32 globals_sb;
        u32 output_image_i;
        u32 chunk_buffer_i;
    };

    struct ChunkRaymarchPush {
        u32 globals_sb;
        u32 output_image_i;
        u32 chunk_buffer_i;
    };

    RenderContext &render_ctx;

    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point start;

    World(RenderContext &render_ctx) : render_ctx(render_ctx) {
        load_textures("DaxaMinecraft/assets/textures");
        load_shaders();

        start = Clock::now();

        for (auto &chunk_layer : chunks) {
            for (auto &chunk_strip : chunk_layer) {
                for (auto &chunk : chunk_strip) {
                    chunk = std::make_unique<RenderableChunk>();
                    chunk->chunkgen_image_a = render_ctx.device->createImageView({
                        .image = render_ctx.device->createImage({
                            .imageType = VK_IMAGE_TYPE_3D,
                            .format = VK_FORMAT_R32_UINT,
                            .extent = {64, 64, 64},
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            .debugName = "Chunkgen Image",
                        }),
                        .viewType = VK_IMAGE_VIEW_TYPE_3D,
                        .format = VK_FORMAT_R32_UINT,
                        .subresourceRange =
                            {
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                        .debugName = "Chunkgen Image View",
                    });
                    chunk->chunkgen_image_b = render_ctx.device->createImageView({
                        .image = render_ctx.device->createImage({
                            .imageType = VK_IMAGE_TYPE_3D,
                            .format = VK_FORMAT_R32_UINT,
                            .extent = {64, 64, 64},
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            .debugName = "Chunkgen Image",
                        }),
                        .viewType = VK_IMAGE_VIEW_TYPE_3D,
                        .format = VK_FORMAT_R32_UINT,
                        .subresourceRange =
                            {
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                        .debugName = "Chunkgen Image View",
                    });
                }
            }
        }

        auto cmd_list = render_ctx.queue->getCommandList({});

        for (auto &chunk_layer : chunks) {
            for (auto &chunk_strip : chunk_layer) {
                for (auto &chunk : chunk_strip) {
                    cmd_list->queueImageBarrier(daxa::ImageBarrier{
                        .barrier = daxa::FULL_MEMORY_BARRIER,
                        .image = chunk->chunkgen_image_a,
                        .layoutBefore = VK_IMAGE_LAYOUT_UNDEFINED,
                        .layoutAfter = VK_IMAGE_LAYOUT_GENERAL,
                    });
                    cmd_list->queueImageBarrier(daxa::ImageBarrier{
                        .barrier = daxa::FULL_MEMORY_BARRIER,
                        .image = chunk->chunkgen_image_b,
                        .layoutBefore = VK_IMAGE_LAYOUT_UNDEFINED,
                        .layoutAfter = VK_IMAGE_LAYOUT_GENERAL,
                    });
                }
            }
        }

        cmd_list->finalize();
        daxa::SubmitInfo submitInfo;
        submitInfo.commandLists.push_back(std::move(cmd_list));
        render_ctx.queue->submit(submitInfo);

        compute_pipeline_globals = render_ctx.device->createBuffer({
            .size = sizeof(ComputeGlobals),
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        });
    }

    ~World() {
        render_ctx.queue->waitIdle();
        render_ctx.queue->checkForFinishedSubmits();
    }

    void reload_shaders() {
        if (render_ctx.pipeline_compiler->checkIfSourcesChanged(raymarch_compute_pipeline)) {
            auto result = render_ctx.pipeline_compiler->recreatePipeline(raymarch_compute_pipeline);
            std::cout << result << std::endl;
            if (result)
                raymarch_compute_pipeline = result.value();
        }
        if (render_ctx.pipeline_compiler->checkIfSourcesChanged(chunkgen_compute_pipeline_pass0)) {
            auto result = render_ctx.pipeline_compiler->recreatePipeline(chunkgen_compute_pipeline_pass0);
            std::cout << result << std::endl;
            if (result)
                chunkgen_compute_pipeline_pass0 = result.value();
        }
        if (render_ctx.pipeline_compiler->checkIfSourcesChanged(chunkgen_compute_pipeline_pass1)) {
            auto result = render_ctx.pipeline_compiler->recreatePipeline(chunkgen_compute_pipeline_pass1);
            std::cout << result << std::endl;
            if (result)
                chunkgen_compute_pipeline_pass1 = result.value();
        }
        for (auto &sdf_pipe : sdf_compute_pipeline_passes) {
            if (render_ctx.pipeline_compiler->checkIfSourcesChanged(sdf_pipe)) {
                auto result = render_ctx.pipeline_compiler->recreatePipeline(sdf_pipe);
                std::cout << result << std::endl;
                if (result)
                    sdf_pipe = result.value();
            }
        }
    }

    void update(float) {
        reload_shaders();
    }

    void draw(const glm::mat4 &vp_mat, const Player3D &player, daxa::CommandListHandle cmd_list, daxa::ImageViewHandle &render_image) {
        auto extent = render_image->getImageHandle()->getVkExtent3D();

        auto elapsed = std::chrono::duration<float>(Clock::now() - start).count();

        auto compute_globals = ComputeGlobals{
            .viewproj_mat = vp_mat,
            .pos = glm::vec4(player.pos, 0),
            .frame_dim = {extent.width, extent.height},
            .time = elapsed,
            .texture_index = atlas_texture_array->getDescriptorIndex(),
        };

        for (size_t zi = 0; zi < DIM.z; ++zi) {
            for (size_t yi = 0; yi < DIM.y; ++yi) {
                for (size_t xi = 0; xi < DIM.x; ++xi) {
                    compute_globals.chunk_ids[0][zi][yi][xi] = chunks[zi][yi][xi]->chunkgen_image_a->getDescriptorIndex();
                    compute_globals.chunk_ids[1][zi][yi][xi] = chunks[zi][yi][xi]->chunkgen_image_b->getDescriptorIndex();
                }
            }
        }

        auto compute_globals_i = compute_pipeline_globals->getDescriptorIndex();

        cmd_list->singleCopyHostToBuffer({
            .src = reinterpret_cast<u8 *>(&compute_globals),
            .dst = compute_pipeline_globals,
            .region = {.size = sizeof(decltype(compute_globals))},
        });
        cmd_list->queueMemoryBarrier(daxa::FULL_MEMORY_BARRIER);

        cmd_list->bindPipeline(chunkgen_compute_pipeline_pass0);
        cmd_list->bindAll();
        for (size_t zi = 0; zi < DIM.z; ++zi) {
            for (size_t yi = 0; yi < DIM.y; ++yi) {
                for (size_t xi = 0; xi < DIM.x; ++xi) {
                    cmd_list->pushConstant(
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        ChunkgenPush{
                            .pos = {64.0f * xi, 64.0f * yi, 64.0f * zi, 0},
                            .globals_sb = compute_globals_i,
                            .output_image_i = compute_globals.chunk_ids[0][zi][yi][xi],
                            .chunk_buffer_i = 1,
                        });
                    cmd_list->dispatch(8, 8, 8);
                }
            }
        }

        cmd_list->queueMemoryBarrier(daxa::FULL_MEMORY_BARRIER);

        cmd_list->bindPipeline(chunkgen_compute_pipeline_pass1);
        cmd_list->bindAll();
        for (size_t zi = 0; zi < DIM.z; ++zi) {
            for (size_t yi = 0; yi < DIM.y; ++yi) {
                for (size_t xi = 0; xi < DIM.x; ++xi) {
                    cmd_list->pushConstant(
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        ChunkgenPush{
                            .pos = {64.0f * xi, 64.0f * yi, 64.0f * zi, 0},
                            .globals_sb = compute_globals_i,
                            .output_image_i = compute_globals.chunk_ids[1][zi][yi][xi],
                            .chunk_buffer_i = 0,
                        });
                    cmd_list->dispatch(8, 8, 8);
                }
            }
        }

        for (auto &sdf_pass_pipe : sdf_compute_pipeline_passes) {
            cmd_list->queueMemoryBarrier(daxa::FULL_MEMORY_BARRIER);
            cmd_list->bindPipeline(sdf_pass_pipe);
            cmd_list->bindAll();
            for (size_t zi = 0; zi < DIM.z; ++zi) {
                for (size_t yi = 0; yi < DIM.y; ++yi) {
                    for (size_t xi = 0; xi < DIM.x; ++xi) {
                        cmd_list->pushConstant(
                            VK_SHADER_STAGE_COMPUTE_BIT,
                            ChunkgenPush{
                                .pos = {64.0f * xi, 64.0f * yi, 64.0f * zi, 0},
                                .globals_sb = compute_globals_i,
                                .output_image_i = compute_globals.chunk_ids[1][zi][yi][xi],
                                .chunk_buffer_i = 1,
                            });
                        cmd_list->dispatch(8, 8, 8);
                    }
                }
            }
        }

        cmd_list->queueMemoryBarrier(daxa::FULL_MEMORY_BARRIER);

        cmd_list->bindPipeline(raymarch_compute_pipeline);
        cmd_list->bindAll();
        cmd_list->pushConstant(
            VK_SHADER_STAGE_COMPUTE_BIT,
            ChunkRaymarchPush{
                .globals_sb = compute_globals_i,
                .output_image_i = render_image->getDescriptorIndex(),
                .chunk_buffer_i = 1,
            });
        cmd_list->dispatch((extent.width + 7) / 8, (extent.height + 7) / 8);
    }

    void load_shaders() {
        {
            auto result = render_ctx.pipeline_compiler->createComputePipeline({
                .shaderCI = {
                    .pathToSource = "DaxaMinecraft/assets/shaders/drawing/raymarch.comp",
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                .overwriteSets = {daxa::BIND_ALL_SET_DESCRIPTION},
            });
            raymarch_compute_pipeline = result.value();
        }
        {
            auto result = render_ctx.pipeline_compiler->createComputePipeline({
                .shaderCI = {
                    .pathToSource = "DaxaMinecraft/assets/shaders/chunkgen/blocks_pass1.comp",
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                .overwriteSets = {daxa::BIND_ALL_SET_DESCRIPTION},
            });
            chunkgen_compute_pipeline_pass0 = result.value();
        }
        {
            auto result = render_ctx.pipeline_compiler->createComputePipeline({
                .shaderCI = {
                    .pathToSource = "DaxaMinecraft/assets/shaders/chunkgen/blocks_pass2.comp",
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                .overwriteSets = {daxa::BIND_ALL_SET_DESCRIPTION},
            });
            chunkgen_compute_pipeline_pass1 = result.value();
        }

        std::filesystem::path sdf_pass_paths[3] = {
            "DaxaMinecraft/assets/shaders/chunkgen/sdf_pass0.comp",
            "DaxaMinecraft/assets/shaders/chunkgen/sdf_pass1.comp",
            "DaxaMinecraft/assets/shaders/chunkgen/sdf_pass2.comp",
        };

        for (size_t i = 0; i < 3; ++i) {
            auto result = render_ctx.pipeline_compiler->createComputePipeline({
                .shaderCI = {
                    .pathToSource = sdf_pass_paths[i],
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                .overwriteSets = {daxa::BIND_ALL_SET_DESCRIPTION},
            });
            sdf_compute_pipeline_passes[i] = result.value();
        }
    }

    void load_textures(const std::filesystem::path &filepath) {
        atlas_texture_array = render_ctx.device->createImageView({
            .image = render_ctx.device->createImage({
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .extent = {16, 16, 1},
                .mipLevels = 4,
                .arrayLayers = 19,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .debugName = "Texture Image",
            }),
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 4,
                    .baseArrayLayer = 0,
                    .layerCount = 19,
                },
            .defaultSampler = render_ctx.device->createSampler({
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_LINEAR,
                .anisotropyEnable = true,
                .maxAnisotropy = 16.0f,
                .maxLod = 3,
                .debugName = "Texture Sampler",
            }),
            .debugName = "Texture Image View",
        });

        auto cmd_list = render_ctx.queue->getCommandList({});
        cmd_list->insertImageBarrier({
            .image = atlas_texture_array,
            .layoutAfter = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .subRange = {VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 19,
            }},
        });

        std::array<std::filesystem::path, 19> texture_names{
            "brick.png",
            "cactus.png",
            "cobblestone.png",
            "diamond_ore.png",
            "dirt.png",
            "dried_shrub.png",
            "grass-side.png",
            "grass-top.png",
            "gravel.png",
            "leaves.png",
            "log-side.png",
            "log-top.png",
            "planks.png",
            "rose.png",
            "sand.png",
            "sandstone.png",
            "stone.png",
            "tallgrass.png",
            "water.png",
        };

        for (size_t i = 0; i < 19; ++i) {
            stbi_set_flip_vertically_on_load(true);
            auto path = filepath / texture_names[i];
            int size_x, size_y, num_channels;
            std::uint8_t *data = stbi_load(path.string().c_str(), &size_x, &size_y, &num_channels, 0);
            cmd_list->copyHostToImage({
                .src = data,
                .dst = atlas_texture_array,
                .dstImgSubressource = {VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = static_cast<uint32_t>(i),
                    .layerCount = 1,
                }},
                .size = static_cast<u32>(size_x * size_y * num_channels) * sizeof(uint8_t),
            });
        }
        daxa::generateMipLevels(
            cmd_list, atlas_texture_array,
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 19,
            },
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cmd_list->finalize();
        render_ctx.queue->submitBlocking({
            .commandLists = {cmd_list},
        });
        render_ctx.queue->checkForFinishedSubmits();
    }
};