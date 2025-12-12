#pragma once

#include "VulkanTypes.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanDescriptors.h"
#include <string>

namespace myth {
namespace vk {

class VulkanPipeline {
public:
    void init(VulkanContext* ctx, VulkanSwapchain* swapchain, DescriptorManager* descriptors,
              const std::string& vertPath, const std::string& fragPath);
    void destroy();
    
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& path);

    VulkanContext* m_context = nullptr;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace myth
