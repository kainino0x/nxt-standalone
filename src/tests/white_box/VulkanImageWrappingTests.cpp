// Copyright 2019 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tests/DawnTest.h"

#include "common/vulkan_platform.h"
#include "dawn_native/VulkanBackend.h"
#include "dawn_native/vulkan/AdapterVk.h"
#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/FencedDeleter.h"
#include "dawn_native/vulkan/MemoryAllocator.h"
#include "dawn_native/vulkan/TextureVk.h"
#include "utils/DawnHelpers.h"
#include "utils/SystemUtils.h"

// TODO(crbug.com/966500): Intel is disabled until upgrade is finished

namespace {

    class VulkanImageWrappingTestBase : public DawnTest {
      public:
        void SetUp() override {
            DawnTest::SetUp();
            if (UsesWire() || IsIntel()) {
                return;
            }

            deviceVk = reinterpret_cast<dawn_native::vulkan::Device*>(device.Get());
        }

        // Creates a VkImage with external memory
        VkResult CreateImage(dawn_native::vulkan::Device* deviceVk,
                             uint32_t width,
                             uint32_t height,
                             VkFormat format,
                             VkImage* image) {
            VkExternalMemoryImageCreateInfoKHR externalInfo;
            externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
            externalInfo.pNext = nullptr;
            externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

            auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            VkImageCreateInfo createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            createInfo.pNext = &externalInfo;
            createInfo.flags = VK_IMAGE_CREATE_ALIAS_BIT_KHR;
            createInfo.imageType = VK_IMAGE_TYPE_2D;
            createInfo.format = format;
            createInfo.extent = {width, height, 1};
            createInfo.mipLevels = 1;
            createInfo.arrayLayers = 1;
            createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            createInfo.usage = usage;
            createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
            createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            return deviceVk->fn.CreateImage(deviceVk->GetVkDevice(), &createInfo, nullptr, image);
        }

        // Allocates memory for an image
        VkResult AllocateMemory(dawn_native::vulkan::Device* deviceVk,
                                VkImage handle,
                                VkDeviceMemory* allocation,
                                VkDeviceSize* allocationSize,
                                uint32_t* memoryTypeIndex) {
            // Create the image memory and associate it with the container
            VkMemoryRequirements requirements;
            deviceVk->fn.GetImageMemoryRequirements(deviceVk->GetVkDevice(), handle, &requirements);

            // Import memory from file descriptor
            VkExportMemoryAllocateInfoKHR externalInfo;
            externalInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
            externalInfo.pNext = nullptr;
            externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

            int bestType = deviceVk->GetMemoryAllocator()->FindBestTypeIndex(requirements, false);
            VkMemoryAllocateInfo allocateInfo;
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.pNext = &externalInfo;
            allocateInfo.allocationSize = requirements.size;
            allocateInfo.memoryTypeIndex = static_cast<uint32_t>(bestType);

            *allocationSize = allocateInfo.allocationSize;
            *memoryTypeIndex = allocateInfo.memoryTypeIndex;

            return deviceVk->fn.AllocateMemory(deviceVk->GetVkDevice(), &allocateInfo, nullptr,
                                               allocation);
        }

        // Binds memory to an image
        VkResult BindMemory(dawn_native::vulkan::Device* deviceVk,
                            VkImage handle,
                            VkDeviceMemory* memory) {
            return deviceVk->fn.BindImageMemory(deviceVk->GetVkDevice(), handle, *memory, 0);
        }

        // Extracts a file descriptor representing memory on a device
        int GetMemoryFd(dawn_native::vulkan::Device* deviceVk, VkDeviceMemory memory) {
            VkMemoryGetFdInfoKHR getFdInfo;
            getFdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
            getFdInfo.pNext = nullptr;
            getFdInfo.memory = memory;
            getFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

            int memoryFd = -1;
            deviceVk->fn.GetMemoryFdKHR(deviceVk->GetVkDevice(), &getFdInfo, &memoryFd);

            EXPECT_GE(memoryFd, 0) << "Failed to get file descriptor for external memory";
            return memoryFd;
        }

        // Prepares and exports memory for an image on a given device
        void CreateBindExportImage(dawn_native::vulkan::Device* deviceVk,
                                   uint32_t width,
                                   uint32_t height,
                                   VkFormat format,
                                   VkImage* handle,
                                   VkDeviceMemory* allocation,
                                   VkDeviceSize* allocationSize,
                                   uint32_t* memoryTypeIndex,
                                   int* memoryFd) {
            VkResult result = CreateImage(deviceVk, width, height, format, handle);
            EXPECT_EQ(result, VK_SUCCESS) << "Failed to create external image";

            VkResult resultBool =
                AllocateMemory(deviceVk, *handle, allocation, allocationSize, memoryTypeIndex);
            EXPECT_EQ(resultBool, VK_SUCCESS) << "Failed to allocate external memory";

            result = BindMemory(deviceVk, *handle, allocation);
            EXPECT_EQ(result, VK_SUCCESS) << "Failed to bind image memory";

            *memoryFd = GetMemoryFd(deviceVk, *allocation);
        }

        // Wraps a vulkan image from external memory
        dawn::Texture WrapVulkanImage(dawn::Device device,
                                      const dawn::TextureDescriptor* textureDescriptor,
                                      int memoryFd,
                                      VkDeviceSize allocationSize,
                                      uint32_t memoryTypeIndex,
                                      std::vector<int> waitFDs,
                                      bool isCleared = true,
                                      bool expectValid = true) {
            dawn_native::vulkan::ExternalImageDescriptorOpaqueFD descriptor;
            descriptor.cTextureDescriptor =
                reinterpret_cast<const DawnTextureDescriptor*>(textureDescriptor);
            descriptor.isCleared = isCleared;
            descriptor.allocationSize = allocationSize;
            descriptor.memoryTypeIndex = memoryTypeIndex;
            descriptor.memoryFD = memoryFd;
            descriptor.waitFDs = waitFDs;

            DawnTexture texture =
                dawn_native::vulkan::WrapVulkanImageOpaqueFD(device.Get(), &descriptor);

            if (expectValid) {
                EXPECT_NE(texture, nullptr) << "Failed to wrap image, are external memory / "
                                               "semaphore extensions supported?";
            } else {
                EXPECT_EQ(texture, nullptr);
            }

            return dawn::Texture::Acquire(texture);
        }

        // Exports the signal from a wrapped texture and ignores it
        // We have to export the signal before destroying the wrapped texture else it's an assertion
        // failure
        void IgnoreSignalSemaphore(dawn::Device device, dawn::Texture wrappedTexture) {
            int fd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(device.Get(),
                                                                        wrappedTexture.Get());
            ASSERT_NE(fd, -1);
            close(fd);
        }

      protected:
        dawn_native::vulkan::Device* deviceVk;
    };

}  // anonymous namespace

class VulkanImageWrappingValidationTests : public VulkanImageWrappingTestBase {
  public:
    void SetUp() override {
        VulkanImageWrappingTestBase::SetUp();
        if (UsesWire() || IsIntel()) {
            return;
        }

        CreateBindExportImage(deviceVk, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &defaultImage,
                              &defaultAllocation, &defaultAllocationSize, &defaultMemoryTypeIndex,
                              &defaultFd);
        defaultDescriptor.dimension = dawn::TextureDimension::e2D;
        defaultDescriptor.format = dawn::TextureFormat::RGBA8Unorm;
        defaultDescriptor.size = {1, 1, 1};
        defaultDescriptor.sampleCount = 1;
        defaultDescriptor.arrayLayerCount = 1;
        defaultDescriptor.mipLevelCount = 1;
        defaultDescriptor.usage = dawn::TextureUsageBit::OutputAttachment |
                                  dawn::TextureUsageBit::CopySrc | dawn::TextureUsageBit::CopyDst;
    }

    void TearDown() override {
        if (UsesWire() || IsIntel()) {
            VulkanImageWrappingTestBase::TearDown();
            return;
        }

        deviceVk->GetFencedDeleter()->DeleteWhenUnused(defaultImage);
        deviceVk->GetFencedDeleter()->DeleteWhenUnused(defaultAllocation);
        VulkanImageWrappingTestBase::TearDown();
    }

  protected:
    dawn::TextureDescriptor defaultDescriptor;
    VkImage defaultImage;
    VkDeviceMemory defaultAllocation;
    VkDeviceSize defaultAllocationSize;
    uint32_t defaultMemoryTypeIndex;
    int defaultFd;
};

// Test no error occurs if the import is valid
TEST_P(VulkanImageWrappingValidationTests, SuccessfulImport) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    dawn::Texture texture =
        WrapVulkanImage(device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {}, true, true);
    EXPECT_NE(texture.Get(), nullptr);
    IgnoreSignalSemaphore(device, texture);
}

// Test an error occurs if the texture descriptor is missing
TEST_P(VulkanImageWrappingValidationTests, MissingTextureDescriptor) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    ASSERT_DEVICE_ERROR(dawn::Texture texture =
                            WrapVulkanImage(device, nullptr, defaultFd, defaultAllocationSize,
                                            defaultMemoryTypeIndex, {}, true, false));
    EXPECT_EQ(texture.Get(), nullptr);
}

// Test an error occurs if the texture descriptor is invalid
TEST_P(VulkanImageWrappingValidationTests, InvalidTextureDescriptor) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    defaultDescriptor.nextInChain = this;

    ASSERT_DEVICE_ERROR(dawn::Texture texture = WrapVulkanImage(
                            device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                            defaultMemoryTypeIndex, {}, true, false));
    EXPECT_EQ(texture.Get(), nullptr);
}

// Test an error occurs if the descriptor dimension isn't 2D
TEST_P(VulkanImageWrappingValidationTests, InvalidTextureDimension) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    defaultDescriptor.dimension = dawn::TextureDimension::e1D;

    ASSERT_DEVICE_ERROR(dawn::Texture texture = WrapVulkanImage(
                            device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                            defaultMemoryTypeIndex, {}, true, false));
    EXPECT_EQ(texture.Get(), nullptr);
}

// Test an error occurs if the descriptor mip level count isn't 1
TEST_P(VulkanImageWrappingValidationTests, InvalidMipLevelCount) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    defaultDescriptor.mipLevelCount = 2;

    ASSERT_DEVICE_ERROR(dawn::Texture texture = WrapVulkanImage(
                            device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                            defaultMemoryTypeIndex, {}, true, false));
    EXPECT_EQ(texture.Get(), nullptr);
}

// Test an error occurs if the descriptor array layer count isn't 1
TEST_P(VulkanImageWrappingValidationTests, InvalidArrayLayerCount) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    defaultDescriptor.arrayLayerCount = 2;

    ASSERT_DEVICE_ERROR(dawn::Texture texture = WrapVulkanImage(
                            device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                            defaultMemoryTypeIndex, {}, true, false));
    EXPECT_EQ(texture.Get(), nullptr);
}

// Test an error occurs if the descriptor sample count isn't 1
TEST_P(VulkanImageWrappingValidationTests, InvalidSampleCount) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    defaultDescriptor.sampleCount = 4;

    ASSERT_DEVICE_ERROR(dawn::Texture texture = WrapVulkanImage(
                            device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                            defaultMemoryTypeIndex, {}, true, false));
    EXPECT_EQ(texture.Get(), nullptr);
}

// Test an error occurs if we try to export the signal semaphore twice
TEST_P(VulkanImageWrappingValidationTests, DoubleSignalSemaphoreExport) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    dawn::Texture texture =
        WrapVulkanImage(device, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {}, true, true);
    ASSERT_NE(texture.Get(), nullptr);
    IgnoreSignalSemaphore(device, texture);
    ASSERT_DEVICE_ERROR(
        int fd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(device.Get(), texture.Get()));
    ASSERT_EQ(fd, -1);
}

// Test an error occurs if we try to export the signal semaphore from a normal texture
TEST_P(VulkanImageWrappingValidationTests, NormalTextureSignalSemaphoreExport) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    dawn::Texture texture = device.CreateTexture(&defaultDescriptor);
    ASSERT_NE(texture.Get(), nullptr);
    ASSERT_DEVICE_ERROR(
        int fd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(device.Get(), texture.Get()));
    ASSERT_EQ(fd, -1);
}

// Test an error occurs if we try to export the signal semaphore from a destroyed texture
TEST_P(VulkanImageWrappingValidationTests, DestroyedTextureSignalSemaphoreExport) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    dawn::Texture texture = device.CreateTexture(&defaultDescriptor);
    ASSERT_NE(texture.Get(), nullptr);
    texture.Destroy();
    ASSERT_DEVICE_ERROR(
        int fd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(device.Get(), texture.Get()));
    ASSERT_EQ(fd, -1);
}

// Fixture to test using external memory textures through different usages.
// These tests are skipped if the harness is using the wire.
class VulkanImageWrappingUsageTests : public VulkanImageWrappingTestBase {
  public:
    void SetUp() override {
        VulkanImageWrappingTestBase::SetUp();
        if (UsesWire() || IsIntel()) {
            return;
        }

        // Create another device based on the original
        backendAdapter = reinterpret_cast<dawn_native::vulkan::Adapter*>(deviceVk->GetAdapter());
        deviceDescriptor.forceEnabledToggles = GetParam().forceEnabledWorkarounds;
        deviceDescriptor.forceDisabledToggles = GetParam().forceDisabledWorkarounds;

        secondDeviceVk = reinterpret_cast<dawn_native::vulkan::Device*>(
            backendAdapter->CreateDevice(&deviceDescriptor));
        secondDevice = dawn::Device::Acquire(reinterpret_cast<DawnDevice>(secondDeviceVk));

        CreateBindExportImage(deviceVk, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &defaultImage,
                              &defaultAllocation, &defaultAllocationSize, &defaultMemoryTypeIndex,
                              &defaultFd);
        defaultDescriptor.dimension = dawn::TextureDimension::e2D;
        defaultDescriptor.format = dawn::TextureFormat::RGBA8Unorm;
        defaultDescriptor.size = {1, 1, 1};
        defaultDescriptor.sampleCount = 1;
        defaultDescriptor.arrayLayerCount = 1;
        defaultDescriptor.mipLevelCount = 1;
        defaultDescriptor.usage = dawn::TextureUsageBit::OutputAttachment |
                                  dawn::TextureUsageBit::CopySrc | dawn::TextureUsageBit::CopyDst;
    }

    void TearDown() override {
        if (UsesWire() || IsIntel()) {
            VulkanImageWrappingTestBase::TearDown();
            return;
        }

        deviceVk->GetFencedDeleter()->DeleteWhenUnused(defaultImage);
        deviceVk->GetFencedDeleter()->DeleteWhenUnused(defaultAllocation);
        VulkanImageWrappingTestBase::TearDown();
    }

  protected:
    dawn::Device secondDevice;
    dawn_native::vulkan::Device* secondDeviceVk;

    dawn_native::vulkan::Adapter* backendAdapter;
    dawn_native::DeviceDescriptor deviceDescriptor;

    dawn::TextureDescriptor defaultDescriptor;
    VkImage defaultImage;
    VkDeviceMemory defaultAllocation;
    VkDeviceSize defaultAllocationSize;
    uint32_t defaultMemoryTypeIndex;
    int defaultFd;

    // Clear a texture on a given device
    void ClearImage(dawn::Device device, dawn::Texture wrappedTexture, dawn::Color clearColor) {
        dawn::TextureView wrappedView = wrappedTexture.CreateDefaultView();

        // Submit a clear operation
        utils::ComboRenderPassDescriptor renderPassDescriptor({wrappedView}, {});
        renderPassDescriptor.cColorAttachmentsInfoPtr[0]->clearColor = clearColor;

        dawn::CommandEncoder encoder = device.CreateCommandEncoder();
        dawn::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDescriptor);
        pass.EndPass();

        dawn::CommandBuffer commands = encoder.Finish();

        dawn::Queue queue = device.CreateQueue();
        queue.Submit(1, &commands);
    }

    // Submits a 1x1x1 copy from source to destination
    void SimpleCopyTextureToTexture(dawn::Device device,
                                    dawn::Queue queue,
                                    dawn::Texture source,
                                    dawn::Texture destination) {
        dawn::TextureCopyView copySrc;
        copySrc.texture = source;
        copySrc.mipLevel = 0;
        copySrc.arrayLayer = 0;
        copySrc.origin = {0, 0, 0};

        dawn::TextureCopyView copyDst;
        copyDst.texture = destination;
        copyDst.mipLevel = 0;
        copyDst.arrayLayer = 0;
        copyDst.origin = {0, 0, 0};

        dawn::Extent3D copySize = {1, 1, 1};

        dawn::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyTextureToTexture(&copySrc, &copyDst, &copySize);
        dawn::CommandBuffer commands = encoder.Finish();

        queue.Submit(1, &commands);
    }
};

// Clear an image in |secondDevice|
// Verify clear color is visible in |device|
TEST_P(VulkanImageWrappingUsageTests, ClearImageAcrossDevices) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |secondDevice|
    dawn::Texture wrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |secondDevice|
    ClearImage(secondDevice, wrappedTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    int signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                      wrappedTexture.Get());

    // Import the image to |device|, making sure we wait on signalFd
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture nextWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Verify |device| sees the changes from |secondDevice|
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), nextWrappedTexture, 0, 0);

    IgnoreSignalSemaphore(device, nextWrappedTexture);
}

// Import texture to |device| and |secondDevice|
// Clear image in |secondDevice|
// Verify clear color is visible in |device|
// Verify the very first import into |device| also sees the change, since it should
// alias the same memory
TEST_P(VulkanImageWrappingUsageTests, ClearImageAcrossDevicesAliased) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());
    // Import the image on |device
    dawn::Texture wrappedTextureAlias = WrapVulkanImage(
        device, &defaultDescriptor, defaultFd, defaultAllocationSize, defaultMemoryTypeIndex, {});

    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);

    // Import the image on |secondDevice|
    dawn::Texture wrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |secondDevice|
    ClearImage(secondDevice, wrappedTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    int signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                      wrappedTexture.Get());

    // Import the image to |device|, making sure we wait on signalFd
    memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture nextWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Verify |device| sees the changes from |secondDevice| (waits)
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), nextWrappedTexture, 0, 0);

    // Verify aliased texture sees changes from |secondDevice| (without waiting!)
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), wrappedTextureAlias, 0, 0);

    IgnoreSignalSemaphore(device, nextWrappedTexture);
    IgnoreSignalSemaphore(device, wrappedTextureAlias);
}

// Clear an image in |secondDevice|
// Verify clear color is not visible in |device| if we import the texture as not cleared
TEST_P(VulkanImageWrappingUsageTests, UnclearedTextureIsCleared) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |secondDevice|
    dawn::Texture wrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |secondDevice|
    ClearImage(secondDevice, wrappedTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    int signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                      wrappedTexture.Get());

    // Import the image to |device|, making sure we wait on signalFd
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture nextWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd}, false);

    // Verify |device| doesn't see the changes from |secondDevice|
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(0, 0, 0, 0), nextWrappedTexture, 0, 0);

    IgnoreSignalSemaphore(device, nextWrappedTexture);
}

// Import a texture into |secondDevice|
// Issue a copy of the imported texture inside |device| to |copyDstTexture|
// Verify the clear color from |secondDevice| is visible in |copyDstTexture|
TEST_P(VulkanImageWrappingUsageTests, CopyTextureToTextureSrcSync) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |secondDevice|
    dawn::Texture wrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |secondDevice|
    ClearImage(secondDevice, wrappedTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    int signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                      wrappedTexture.Get());

    // Import the image to |device|, making sure we wait on |signalFd|
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture deviceWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Create a second texture on |device|
    dawn::Texture copyDstTexture = device.CreateTexture(&defaultDescriptor);

    // Copy |deviceWrappedTexture| into |copyDstTexture|
    SimpleCopyTextureToTexture(device, queue, deviceWrappedTexture, copyDstTexture);

    // Verify |copyDstTexture| sees changes from |secondDevice|
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), copyDstTexture, 0, 0);

    IgnoreSignalSemaphore(device, deviceWrappedTexture);
}

// Import a texture into |device|
// Copy color A into texture on |device|
// Import same texture into |secondDevice|, waiting on the copy signal
// Copy color B using Texture to Texture copy on |secondDevice|
// Import texture back into |device|, waiting on color B signal
// Verify texture contains color B
// If texture destination isn't synchronized, |secondDevice| could copy color B
// into the texture first, then |device| writes color A
TEST_P(VulkanImageWrappingUsageTests, CopyTextureToTextureDstSync) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |device|
    dawn::Texture wrappedTexture = WrapVulkanImage(
        device, &defaultDescriptor, defaultFd, defaultAllocationSize, defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |device|
    ClearImage(device, wrappedTexture, {5 / 255.0f, 6 / 255.0f, 7 / 255.0f, 8 / 255.0f});

    int signalFd =
        dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(device.Get(), wrappedTexture.Get());

    // Import the image to |secondDevice|, making sure we wait on |signalFd|
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture secondDeviceWrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Create a texture with color B on |secondDevice|
    dawn::Texture copySrcTexture = secondDevice.CreateTexture(&defaultDescriptor);
    ClearImage(secondDevice, copySrcTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    // Copy color B on |secondDevice|
    dawn::Queue secondDeviceQueue = secondDevice.CreateQueue();
    SimpleCopyTextureToTexture(secondDevice, secondDeviceQueue, copySrcTexture,
                               secondDeviceWrappedTexture);

    // Re-import back into |device|, waiting on |secondDevice|'s signal
    signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                  secondDeviceWrappedTexture.Get());
    memoryFd = GetMemoryFd(deviceVk, defaultAllocation);

    dawn::Texture nextWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Verify |nextWrappedTexture| contains the color from our copy
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), nextWrappedTexture, 0, 0);

    IgnoreSignalSemaphore(device, nextWrappedTexture);
}

// Import a texture from |secondDevice|
// Issue a copy of the imported texture inside |device| to |copyDstBuffer|
// Verify the clear color from |secondDevice| is visible in |copyDstBuffer|
TEST_P(VulkanImageWrappingUsageTests, CopyTextureToBufferSrcSync) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |secondDevice|
    dawn::Texture wrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |secondDevice|
    ClearImage(secondDevice, wrappedTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    int signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                      wrappedTexture.Get());

    // Import the image to |device|, making sure we wait on |signalFd|
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture deviceWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Create a destination buffer on |device|
    dawn::BufferDescriptor bufferDesc;
    bufferDesc.size = 4;
    bufferDesc.usage = dawn::BufferUsageBit::CopyDst | dawn::BufferUsageBit::CopySrc;
    dawn::Buffer copyDstBuffer = device.CreateBuffer(&bufferDesc);

    // Copy |deviceWrappedTexture| into |copyDstBuffer|
    dawn::TextureCopyView copySrc;
    copySrc.texture = deviceWrappedTexture;
    copySrc.mipLevel = 0;
    copySrc.arrayLayer = 0;
    copySrc.origin = {0, 0, 0};

    dawn::BufferCopyView copyDst;
    copyDst.buffer = copyDstBuffer;
    copyDst.offset = 0;
    copyDst.rowPitch = 256;
    copyDst.imageHeight = 0;

    dawn::Extent3D copySize = {1, 1, 1};

    dawn::CommandEncoder encoder = device.CreateCommandEncoder();
    encoder.CopyTextureToBuffer(&copySrc, &copyDst, &copySize);
    dawn::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);

    // Verify |copyDstBuffer| sees changes from |secondDevice|
    uint32_t expected = 0x04030201;
    EXPECT_BUFFER_U32_EQ(expected, copyDstBuffer, 0);

    IgnoreSignalSemaphore(device, deviceWrappedTexture);
}

// Import a texture into |device|
// Copy color A into texture on |device|
// Import same texture into |secondDevice|, waiting on the copy signal
// Copy color B using Buffer to Texture copy on |secondDevice|
// Import texture back into |device|, waiting on color B signal
// Verify texture contains color B
// If texture destination isn't synchronized, |secondDevice| could copy color B
// into the texture first, then |device| writes color A
TEST_P(VulkanImageWrappingUsageTests, CopyBufferToTextureDstSync) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |device|
    dawn::Texture wrappedTexture = WrapVulkanImage(
        device, &defaultDescriptor, defaultFd, defaultAllocationSize, defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |device|
    ClearImage(device, wrappedTexture, {5 / 255.0f, 6 / 255.0f, 7 / 255.0f, 8 / 255.0f});

    int signalFd =
        dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(device.Get(), wrappedTexture.Get());

    // Import the image to |secondDevice|, making sure we wait on |signalFd|
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture secondDeviceWrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Copy color B on |secondDevice|
    dawn::Queue secondDeviceQueue = secondDevice.CreateQueue();

    // Create a buffer on |secondDevice|
    dawn::Buffer copySrcBuffer =
        utils::CreateBufferFromData(secondDevice, dawn::BufferUsageBit::CopySrc, {0x04030201});

    // Copy |copySrcBuffer| into |secondDeviceWrappedTexture|
    dawn::BufferCopyView copySrc;
    copySrc.buffer = copySrcBuffer;
    copySrc.offset = 0;
    copySrc.rowPitch = 256;
    copySrc.imageHeight = 0;

    dawn::TextureCopyView copyDst;
    copyDst.texture = secondDeviceWrappedTexture;
    copyDst.mipLevel = 0;
    copyDst.arrayLayer = 0;
    copyDst.origin = {0, 0, 0};

    dawn::Extent3D copySize = {1, 1, 1};

    dawn::CommandEncoder encoder = secondDevice.CreateCommandEncoder();
    encoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);
    dawn::CommandBuffer commands = encoder.Finish();
    secondDeviceQueue.Submit(1, &commands);

    // Re-import back into |device|, waiting on |secondDevice|'s signal
    signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                  secondDeviceWrappedTexture.Get());
    memoryFd = GetMemoryFd(deviceVk, defaultAllocation);

    dawn::Texture nextWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Verify |nextWrappedTexture| contains the color from our copy
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), nextWrappedTexture, 0, 0);

    IgnoreSignalSemaphore(device, nextWrappedTexture);
}

// Import a texture from |secondDevice|
// Issue a copy of the imported texture inside |device| to |copyDstTexture|
// Issue second copy to |secondCopyDstTexture|
// Verify the clear color from |secondDevice| is visible in both copies
TEST_P(VulkanImageWrappingUsageTests, DoubleTextureUsage) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Import the image on |secondDevice|
    dawn::Texture wrappedTexture =
        WrapVulkanImage(secondDevice, &defaultDescriptor, defaultFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {});

    // Clear |wrappedTexture| on |secondDevice|
    ClearImage(secondDevice, wrappedTexture, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    int signalFd = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(secondDevice.Get(),
                                                                      wrappedTexture.Get());

    // Import the image to |device|, making sure we wait on |signalFd|
    int memoryFd = GetMemoryFd(deviceVk, defaultAllocation);
    dawn::Texture deviceWrappedTexture =
        WrapVulkanImage(device, &defaultDescriptor, memoryFd, defaultAllocationSize,
                        defaultMemoryTypeIndex, {signalFd});

    // Create a second texture on |device|
    dawn::Texture copyDstTexture = device.CreateTexture(&defaultDescriptor);

    // Create a third texture on |device|
    dawn::Texture secondCopyDstTexture = device.CreateTexture(&defaultDescriptor);

    // Copy |deviceWrappedTexture| into |copyDstTexture|
    SimpleCopyTextureToTexture(device, queue, deviceWrappedTexture, copyDstTexture);

    // Copy |deviceWrappedTexture| into |secondCopyDstTexture|
    SimpleCopyTextureToTexture(device, queue, deviceWrappedTexture, secondCopyDstTexture);

    // Verify |copyDstTexture| sees changes from |secondDevice|
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), copyDstTexture, 0, 0);

    // Verify |secondCopyDstTexture| sees changes from |secondDevice|
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), secondCopyDstTexture, 0, 0);

    IgnoreSignalSemaphore(device, deviceWrappedTexture);
}

// Tex A on device 3 (external export)
// Tex B on device 2 (external export)
// Tex C on device 1 (external export)
// Clear color for A on device 3
// Copy A->B on device 3
// Copy B->C on device 2 (wait on B from previous op)
// Copy C->D on device 1 (wait on C from previous op)
// Verify D has same color as A
TEST_P(VulkanImageWrappingUsageTests, ChainTextureCopy) {
    DAWN_SKIP_TEST_IF(UsesWire() || IsIntel());

    // Close |defaultFd| since this test doesn't import it anywhere
    close(defaultFd);

    // device 1 = |device|
    // device 2 = |secondDevice|
    // Create device 3
    dawn_native::vulkan::Device* thirdDeviceVk = reinterpret_cast<dawn_native::vulkan::Device*>(
        backendAdapter->CreateDevice(&deviceDescriptor));
    dawn::Device thirdDevice = dawn::Device::Acquire(reinterpret_cast<DawnDevice>(thirdDeviceVk));

    // Make queue for device 2 and 3
    dawn::Queue secondDeviceQueue = secondDevice.CreateQueue();
    dawn::Queue thirdDeviceQueue = thirdDevice.CreateQueue();

    // Allocate memory for A, B, C
    VkImage imageA;
    VkDeviceMemory allocationA;
    int memoryFdA;
    VkDeviceSize allocationSizeA;
    uint32_t memoryTypeIndexA;
    CreateBindExportImage(thirdDeviceVk, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &imageA, &allocationA,
                          &allocationSizeA, &memoryTypeIndexA, &memoryFdA);

    VkImage imageB;
    VkDeviceMemory allocationB;
    int memoryFdB;
    VkDeviceSize allocationSizeB;
    uint32_t memoryTypeIndexB;
    CreateBindExportImage(secondDeviceVk, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &imageB, &allocationB,
                          &allocationSizeB, &memoryTypeIndexB, &memoryFdB);

    VkImage imageC;
    VkDeviceMemory allocationC;
    int memoryFdC;
    VkDeviceSize allocationSizeC;
    uint32_t memoryTypeIndexC;
    CreateBindExportImage(deviceVk, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &imageC, &allocationC,
                          &allocationSizeC, &memoryTypeIndexC, &memoryFdC);

    // Import TexA, TexB on device 3
    dawn::Texture wrappedTexADevice3 = WrapVulkanImage(thirdDevice, &defaultDescriptor, memoryFdA,
                                                       allocationSizeA, memoryTypeIndexA, {});

    dawn::Texture wrappedTexBDevice3 = WrapVulkanImage(thirdDevice, &defaultDescriptor, memoryFdB,
                                                       allocationSizeB, memoryTypeIndexB, {});

    // Clear TexA
    ClearImage(thirdDevice, wrappedTexADevice3, {1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f});

    // Copy A->B
    SimpleCopyTextureToTexture(thirdDevice, thirdDeviceQueue, wrappedTexADevice3,
                               wrappedTexBDevice3);

    int signalFdTexBDevice3 = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(
        thirdDevice.Get(), wrappedTexBDevice3.Get());
    IgnoreSignalSemaphore(thirdDevice, wrappedTexADevice3);

    // Import TexB, TexC on device 2
    memoryFdB = GetMemoryFd(secondDeviceVk, allocationB);
    dawn::Texture wrappedTexBDevice2 =
        WrapVulkanImage(secondDevice, &defaultDescriptor, memoryFdB, allocationSizeB,
                        memoryTypeIndexB, {signalFdTexBDevice3});

    dawn::Texture wrappedTexCDevice2 = WrapVulkanImage(secondDevice, &defaultDescriptor, memoryFdC,
                                                       allocationSizeC, memoryTypeIndexC, {});

    // Copy B->C on device 2
    SimpleCopyTextureToTexture(secondDevice, secondDeviceQueue, wrappedTexBDevice2,
                               wrappedTexCDevice2);

    int signalFdTexCDevice2 = dawn_native::vulkan::ExportSignalSemaphoreOpaqueFD(
        secondDevice.Get(), wrappedTexCDevice2.Get());
    IgnoreSignalSemaphore(secondDevice, wrappedTexBDevice2);

    // Import TexC on device 1
    memoryFdC = GetMemoryFd(deviceVk, allocationC);
    dawn::Texture wrappedTexCDevice1 =
        WrapVulkanImage(device, &defaultDescriptor, memoryFdC, allocationSizeC, memoryTypeIndexC,
                        {signalFdTexCDevice2});

    // Create TexD on device 1
    dawn::Texture texD = device.CreateTexture(&defaultDescriptor);

    // Copy C->D on device 1
    SimpleCopyTextureToTexture(device, queue, wrappedTexCDevice1, texD);

    // Verify D matches clear color
    EXPECT_PIXEL_RGBA8_EQ(RGBA8(1, 2, 3, 4), texD, 0, 0);

    thirdDeviceVk->GetFencedDeleter()->DeleteWhenUnused(imageA);
    thirdDeviceVk->GetFencedDeleter()->DeleteWhenUnused(allocationA);
    secondDeviceVk->GetFencedDeleter()->DeleteWhenUnused(imageB);
    secondDeviceVk->GetFencedDeleter()->DeleteWhenUnused(allocationB);
    deviceVk->GetFencedDeleter()->DeleteWhenUnused(imageC);
    deviceVk->GetFencedDeleter()->DeleteWhenUnused(allocationC);

    IgnoreSignalSemaphore(device, wrappedTexCDevice1);
}

DAWN_INSTANTIATE_TEST(VulkanImageWrappingValidationTests, VulkanBackend);
DAWN_INSTANTIATE_TEST(VulkanImageWrappingUsageTests, VulkanBackend);