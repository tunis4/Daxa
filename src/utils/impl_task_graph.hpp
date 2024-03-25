#pragma once

#include "../impl_core.hpp"

#include <stack>
#include <daxa/utils/task_graph.hpp>

#define DAXA_TASK_GRAPH_MAX_CONDITIONALS 31

namespace daxa
{
    struct ImplDevice;

    using TaskBatchId = usize;

    using TaskId = usize;

    struct LastConcurrentAccessSplitBarrierIndex
    {
        usize index;
    };

    struct LastConcurrentAccessBarrierIndex
    {
        usize index;
    };

    struct ResourceLifetime
    {
        // TODO(msakmary, pahrens) This will not be needed once we make batches linear and not
        // contained in submit scopes
        struct CombinedBatchIndex
        {
            usize submit_scope_index = std::numeric_limits<u32>::max();
            usize task_batch_index = std::numeric_limits<u32>::max();
        };
        CombinedBatchIndex first_use;
        CombinedBatchIndex last_use;
    };

    enum struct TaskAccessConcurrency
    {
        CONCURRENT,
        EXCLUSIVE
    };

    struct PerPermTaskBuffer
    {
        /// Every permutation always has all buffers but they are not necessarily valid in that permutation.
        /// This boolean is used to check this.
        bool valid = {};
        TaskAccessConcurrency latest_access_concurrent = TaskAccessConcurrency::EXCLUSIVE;
        Access latest_access = AccessConsts::NONE;
        usize latest_access_batch_index = {};
        usize latest_access_submit_scope_index = {};
        usize first_access_batch_index = {};
        usize first_access_submit_scope_index = {};
        Access first_access = AccessConsts::NONE;
        // When the last index was a read and an additional read is followed after,
        // we will combine all barriers into one, which is the first barrier that the first read generates.
        Variant<Monostate, LastConcurrentAccessSplitBarrierIndex, LastConcurrentAccessBarrierIndex> latest_concurrent_access_barrer_index = Monostate{};
        BufferId actual_buffer = {};
        ResourceLifetime lifetime = {};
        usize allocation_offset = {};
    };

    struct ExtendedImageSliceState
    {
        ImageSliceState state = {};
        TaskAccessConcurrency latest_access_concurrent = TaskAccessConcurrency::EXCLUSIVE;
        usize latest_access_batch_index = {};
        usize latest_access_submit_scope_index = {};
        // When the last index was a read and an additional read is followed after,
        // we will combine all barriers into one, which is the first barrier that the first read generates.
        Variant<Monostate, LastConcurrentAccessSplitBarrierIndex, LastConcurrentAccessBarrierIndex> latest_concurrent_access_barrer_index = Monostate{};
    };

    struct PerPermTaskImage
    {
        /// Every permutation always has all buffers but they are not necessarily valid in that permutation.
        /// This boolean is used to check this.
        bool valid = {};
        bool swapchain_semaphore_waited_upon = {};
        std::vector<ExtendedImageSliceState> last_slice_states = {};
        std::vector<ExtendedImageSliceState> first_slice_states = {};
        // only for transient images
        ResourceLifetime lifetime = {};
        ImageCreateFlags create_flags = ImageCreateFlagBits::NONE;
        ImageUsageFlags usage = ImageUsageFlagBits::NONE;
        ImageId actual_image = {};
        usize allocation_offset = {};
    };

    struct TaskBarrier
    {
        // when this ID is invalid, this barrier is NOT an image memory barrier but just a memory barrier.
        // So when ID invalid => memory barrier, ID valid => image memory barrier.
        TaskImageView image_id = {};
        ImageMipArraySlice slice = {};
        ImageLayout layout_before = {};
        ImageLayout layout_after = {};
        Access src_access = {};
        Access dst_access = {};
    };

    struct TaskSplitBarrier : TaskBarrier
    {
        Event split_barrier_state;
    };

    struct ImplTask
    {
        std::unique_ptr<ITask> base_task = {};
        std::vector<std::vector<ImageViewId>> image_view_cache = {};
        // Used to verify image view cache:
        std::vector<std::vector<ImageId>> runtime_images_last_execution = {};
    };

    struct ImplPresentInfo
    {
        std::vector<BinarySemaphore> binary_semaphores = {};
        std::vector<BinarySemaphore> * additional_binary_semaphores = {};
    };

    struct TaskBatch
    {
        std::vector<usize> pipeline_barrier_indices = {};
        std::vector<usize> wait_split_barrier_indices = {};
        std::vector<TaskId> tasks = {};
        std::vector<usize> signal_split_barrier_indices = {};
    };

    struct TaskBatchSubmitScope
    {
        CommandSubmitInfo submit_info = {};
        TaskSubmitInfo user_submit_info = {};
        // These barriers are inserted after all batches and their sync.
        std::vector<usize> last_minute_barrier_indices = {};
        std::vector<TaskBatch> task_batches = {};
        std::vector<u64> used_swapchain_task_images = {};
        std::optional<ImplPresentInfo> present_info = {};
    };

    auto task_image_access_to_layout_access(TaskImageAccess const & access) -> std::tuple<ImageLayout, Access, TaskAccessConcurrency>;
    auto task_buffer_access_to_access(TaskBufferAccess const & access) -> std::pair<Access, TaskAccessConcurrency>;

    struct ImplTaskGraph;

    struct TaskGraphPermutation
    {
        // record time information:
        bool active = {};
        // persistent information:
        TaskImageView swapchain_image = {};
        std::vector<PerPermTaskBuffer> buffer_infos = {};
        std::vector<PerPermTaskImage> image_infos = {};
        std::vector<TaskSplitBarrier> split_barriers = {};
        std::vector<TaskBarrier> barriers = {};
        std::vector<usize> initial_barriers = {};
        // TODO(msakmary, pahrens) - Instead of storing batch submit scopes which contain batches
        // we should make a vector of batches which and a second vector of submit scopes which are
        // just offsets into the batches vector
        std::vector<TaskBatchSubmitScope> batch_submit_scopes = {};
        usize swapchain_image_first_use_submit_scope_index = std::numeric_limits<usize>::max();
        usize swapchain_image_last_use_submit_scope_index = std::numeric_limits<usize>::max();

        void add_task(ImplTaskGraph & task_graph_impl, ImplTask & impl_task, TaskId task_id);
        void submit(TaskSubmitInfo const & info);
        void present(TaskPresentInfo const & info);
    };

    struct ImplPersistentTaskBuffer final : ImplHandle
    {
        ImplPersistentTaskBuffer(TaskBufferInfo a_info);
        ~ImplPersistentTaskBuffer();

        TaskBufferInfo info = {};
        std::vector<BufferId> actual_buffers = {};
        Access latest_access = {};

        // Used to allocate id - because all persistent resources have unique id we need a single point
        // from which they are generated
        static inline std::atomic_uint32_t exec_unique_next_index = 1;
        u32 unique_index = std::numeric_limits<u32>::max();

        static void zero_ref_callback(ImplHandle const * handle);
    };

    struct ImplPersistentTaskImage final : ImplHandle
    {
        ImplPersistentTaskImage(TaskImageInfo const & a_info);
        ~ImplPersistentTaskImage();

        TaskImageInfo info = {};
        // One task buffer can back multiple buffers.
        std::vector<ImageId> actual_images = {};
        // We store runtime information about the previous executions final resource states.
        // This is important, as with conditional execution and temporal resources we need to store this infomation to form correct state transitions.
        std::vector<ImageSliceState> latest_slice_states = {};
        // Only for swapchain images. Runtime data.
        bool waited_on_acquire = {};

        // Used to allocate id - because all persistent resources have unique id we need a single point
        // from which they are generated
        static inline std::atomic_uint32_t exec_unique_next_index = 1;
        u32 unique_index = std::numeric_limits<u32>::max();

        static void zero_ref_callback(ImplHandle const * handle);
    };

    struct PermIndepTaskBufferInfo
    {
        struct Persistent
        {
            TaskBuffer buffer = {};

            auto get() -> ImplPersistentTaskBuffer &
            {
                return **r_cast<ImplPersistentTaskBuffer **>(&buffer);
            }
            auto get() const -> ImplPersistentTaskBuffer const &
            {
                return **r_cast<ImplPersistentTaskBuffer const * const *>(&buffer);
            }
        };
        struct Transient
        {
            TaskTransientBufferInfo info = {};
            MemoryRequirements memory_requirements = {};
        };
        Variant<Persistent, Transient> task_buffer_data;

        inline auto get_name() const -> std::string_view
        {
            if (is_persistent())
            {
                return get_persistent().info.name;
            }
            else
            {
                return daxa::get<Transient>(task_buffer_data).info.name;
            }
        }
        inline auto get_persistent() -> ImplPersistentTaskBuffer &
        {
            return daxa::get<Persistent>(task_buffer_data).get();
        }
        inline auto get_persistent() const -> ImplPersistentTaskBuffer const &
        {
            return daxa::get<Persistent>(task_buffer_data).get();
        }
        inline auto is_persistent() const -> bool
        {
            return daxa::holds_alternative<Persistent>(task_buffer_data);
        }
    };

    struct PermIndepTaskImageInfo
    {
        struct Persistent
        {
            TaskImage image = {};
            auto get() -> ImplPersistentTaskImage &
            {
                return **r_cast<ImplPersistentTaskImage **>(&image);
            }
            auto get() const -> ImplPersistentTaskImage const &
            {
                return **r_cast<ImplPersistentTaskImage const * const *>(&image);
            }
        };
        struct Transient
        {
            TaskTransientImageInfo info = {};
            MemoryRequirements memory_requirements = {};
        };
        Variant<Persistent, Transient> task_image_data;

        inline auto get_name() const -> std::string_view
        {
            if (is_persistent())
            {
                return get_persistent().info.name;
            }
            else
            {
                return daxa::get<Transient>(task_image_data).info.name;
            }
        }
        inline auto get_persistent() -> ImplPersistentTaskImage &
        {
            return daxa::get<Persistent>(task_image_data).get();
        }
        inline auto get_persistent() const -> ImplPersistentTaskImage const &
        {
            return daxa::get<Persistent>(task_image_data).get();
        }
        inline auto is_persistent() const -> bool
        {
            return daxa::holds_alternative<Persistent>(task_image_data);
        }
    };

    struct ImplTaskRuntimeInterface
    {
        // interface:
        ImplTaskGraph & task_graph;
        TaskGraphPermutation & permutation;
        CommandRecorder & recorder;
        ImplTask * current_task = {};
        types::DeviceAddress device_address = {};
        bool reuse_last_command_list = true;
        std::optional<BinarySemaphore> last_submit_semaphore = {};
    };

    struct ImplTaskGraph final : ImplHandle
    {
        ImplTaskGraph(TaskGraphInfo a_info);
        ~ImplTaskGraph();

        static inline std::atomic_uint32_t exec_unique_next_index = 1;
        u32 unique_index = {};

        TaskGraphInfo info;
        std::vector<PermIndepTaskBufferInfo> global_buffer_infos = {};
        std::vector<PermIndepTaskImageInfo> global_image_infos = {};
        std::vector<TaskGraphPermutation> permutations = {};
        std::vector<ImplTask> tasks = {};
        // TODO: replace with faster hash map.
        std::unordered_map<u32, u32> persistent_buffer_index_to_local_index;
        std::unordered_map<u32, u32> persistent_image_index_to_local_index;

        // record time information:
        u32 record_active_conditional_scopes = {};
        u32 record_conditional_states = {};
        std::vector<TaskGraphPermutation *> record_active_permutations = {};
        std::unordered_map<std::string, TaskBufferView> buffer_name_to_id = {};
        std::unordered_map<std::string, TaskImageView> image_name_to_id = {};

        usize memory_block_size = {};
        u32 memory_type_bits = 0xFFFFFFFFu;
        MemoryBlock transient_data_memory_block = {};
        bool compiled = {};

        // execution time information:
        std::optional<daxa::TransferMemoryPool> staging_memory = {};
        std::array<bool, DAXA_TASK_GRAPH_MAX_CONDITIONALS> execution_time_current_conditionals = {};

        // post execution information:
        u32 chosen_permutation_last_execution = {};
        std::vector<ExecutableCommandList> left_over_command_lists = {};
        bool executed_once = {};
        u32 prev_frame_permutation_index = {};
        std::stringstream debug_string_stream = {};

        auto get_actual_buffers(TaskBufferView id, TaskGraphPermutation const & perm) const -> std::span<BufferId const>;
        auto get_actual_images(TaskImageView id, TaskGraphPermutation const & perm) const -> std::span<ImageId const>;
        auto id_to_local_id(TaskBufferView id) const -> TaskBufferView;
        auto id_to_local_id(TaskImageView id) const -> TaskImageView;
        void update_active_permutations();
        void update_image_view_cache(ImplTask & task, TaskGraphPermutation const & permutation);
        void execute_task(ImplTaskRuntimeInterface & impl_runtime, TaskGraphPermutation & permutation, TaskBatchId in_batch_task_index, TaskId task_id);
        void insert_pre_batch_barriers(TaskGraphPermutation & permutation);
        void create_transient_runtime_buffers(TaskGraphPermutation & permutation);
        void create_transient_runtime_images(TaskGraphPermutation & permutation);
        void allocate_transient_resources();
        void print_task_buffer_to(std::string & out, std::string indent, TaskGraphPermutation const & permutation, TaskBufferView local_id);
        void print_task_image_to(std::string & out, std::string indent, TaskGraphPermutation const & permutation, TaskImageView image);
        void print_task_barrier_to(std::string & out, std::string & indent, TaskGraphPermutation const & permutation, usize index, bool const split_barrier);
        void print_task_to(std::string & out, std::string & indent, TaskGraphPermutation const & permutation, TaskId task_id);
        void print_permutation_aliasing_to(std::string & out, std::string indent, TaskGraphPermutation const & permutation);
        void debug_print();

        static void zero_ref_callback(ImplHandle const * handle);
    };

} // namespace daxa
