{
  "targets": [
    {
      "target_name": "dawn",
      "include_dirs": [
        "../examples",
        "../out/Debug/gen",
        "../src",
        "../src/include",
        "../third_party/glfw/include",
      ],
      "defines": [
        "DAWN_ENABLE_BACKEND_NULL",
      ],
      "sources": [
        "dawn.cpp",
        "../examples/SampleUtils.h",
        "../examples/SampleUtils.cpp",
        "../src/utils/BackendBinding.h",
        "../src/utils/BackendBinding.cpp",
        "../src/utils/NullBinding.cpp",
      ],
      "libraries": [
        "<!(pwd)/../out/Debug/libdawn_sample.so",
        "<!(pwd)/../out/Debug/obj/libdawn_utils.a",
        "<!(pwd)/../out/Debug/obj/third_party/libglfw.a",
      ],
    }
  ]
}
