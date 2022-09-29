#include "impl_timeline_query.hpp"
#include "impl_device.hpp"

namespace daxa
{
    TimelineQueryPool::TimelineQueryPool(ManagedPtr impl) : ManagedPtr(std::move(impl)){};

    auto TimelineQueryPool::info() const -> TimelineQueryPoolInfo const &
    {
        auto & impl = *as<ImplTimelineQueryPool>();
        return impl.info;
    }

    // NOTE(msakmary) should this be in device instead to avoid having to friend ImplQueryPool in QueryPool?
    auto TimelineQueryPool::get_query_results(u32 start_index, u32 count) -> std::vector<u64>
    {
        auto & impl = *as<ImplTimelineQueryPool>();
        DAXA_DBG_ASSERT_TRUE_M(start_index + count - 1 < impl.info.query_count, "attempting to query results that are out of bound for given pool");

        std::vector<u64> results(count * 2);
        vkGetQueryPoolResults(
            impl.impl_device.as<ImplDevice>()->vk_device,
            impl.vk_timeline_query_pool,
            0,
            count,
            count * sizeof(u64),
            results.data(),
            0,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        return results;
    }

    ImplTimelineQueryPool::ImplTimelineQueryPool(ManagedWeakPtr a_impl_device, TimelineQueryPoolInfo const & info)
        : impl_device{a_impl_device}, info{info}
    {
        // TODO(msakmary) Should Add a check for support of timeline querries
        //                here or earliner (during device creation/section) I'm not sure...
        VkQueryPoolCreateInfo vk_querry_pool_create_info{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = info.query_count};

        vkCreateQueryPool(impl_device.as<ImplDevice>()->vk_device, &vk_querry_pool_create_info, nullptr, &vk_timeline_query_pool);
        vkResetQueryPool(impl_device.as<ImplDevice>()->vk_device, vk_timeline_query_pool, 0, info.query_count);

        if (this->impl_device.as<ImplDevice>()->impl_ctx.as<ImplContext>()->enable_debug_names && info.debug_name.size() > 0)
        {
            VkDebugUtilsObjectNameInfoEXT querry_pool_name_info{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .pNext = nullptr,
                .objectType = VK_OBJECT_TYPE_QUERY_POOL,
                .objectHandle = reinterpret_cast<uint64_t>(vk_timeline_query_pool),
                .pObjectName = info.debug_name.c_str(),
            };
            this->impl_device.as<ImplDevice>()->vkSetDebugUtilsObjectNameEXT(impl_device.as<ImplDevice>()->vk_device, &querry_pool_name_info);
        }
    }

    ImplTimelineQueryPool::~ImplTimelineQueryPool()
    {
        auto device = this->impl_device.as<ImplDevice>();
        DAXA_ONLY_IF_THREADSAFETY(std::unique_lock lock{device->main_queue_zombies_mtx});
        u64 main_queue_cpu_timeline = DAXA_ATOMIC_FETCH(device->main_queue_cpu_timeline);

        device->main_queue_timeline_query_pool_zombies.push_back({
            main_queue_cpu_timeline,
            TimelineQueryPoolZombie{
                .vk_timeline_query_pool = vk_timeline_query_pool,
            },
        });
    }
} // namespace daxa