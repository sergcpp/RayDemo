# Changelog

## [Unreleased]

### Added

- This CHANGELOG file

### Fixed
### Changed
### Removed

## [0.4.5] - 2023-02-13

### Added

  - Shadow ray tracing for untextured environment
  - IOR stack to handle intersecting objects with refraction
  - Camera exposure parameter
  - Universal binary build for MacOS (using lipo)
  - Support for multiple cameras
  - Environment map rotation
  - Option to limit texture resolution
  - Camera sensor shift support
  - Separate background map in scene description
  - Italian flat scene (from Blender demo files)
  - Shader cross-compilation to HLSL (unused for now)
  - GPU timers

### Fixed

  - Transparent shadow artifacts
  - Parallel build issues with MSVC
  - Stack overflow in debug builds using AVX512
  - Windows clang compilation

### Changed

  - Transparency is handled during ray traversal instead of shading
  - Sponza GPU test is enabled on windows (with texture resolution limit)
  - Some CPU tests are running through Intel SDE

## [0.4.0] - 2022-12-11

### Added

  - Line light
  - Importance sampled HDRI lighting (using quadtree)
  - Texture compression for GPU backend
  - Textured specular to Principled material
  - Camera clip start/end
  - Camera depth of field
  - Debug names to bindless textures
  - Option for light to not cast shadow

### Fixed

  - Fireflies in GPU renderer with AMD
  - Validation errors regarding null buffers
  - Not working old scenes
  - Invisible lights visibility through transparencies
  - Clang parameter warning

### Changed

  - Some tests are checked in Debug mode now
  - More optimal AnyHit traversal

## [0.3.5] - 2022-11-16

### Added

  - Hardware accelerated raytracing in Vulkan renderer
  - Swizzled texture layout for better CPU cache utilization
  - Compile-time switches to include/exclude backends
  - BC5 texture compression for normal maps
  - Bindless textures in GPU backend
  - Fallback to CPU-visible memory on allocation fail
  - Test for textured opacity
  - Regex device matching

### Fixed

  - Failed nightly tests on Arc GPU
  - Descriptor sets leak
  - Memory usage spike during scene loading

### Changed

  - Texture pages in non-bindless GPU mode are allocated on demand (starting from dummy texture)
  - Normalmaps are stored as 2-channel textures (z is reconstructed)
  - Device name matched strictly in tests
  - GPU backends are running big scene tests

### Removed

  - Texture atlases in CPU backends (textures are stored individually)

## [0.3.0] - 2022-09-12

### Added

  - Vulkan compute renderer (no HWRT)

### Removed

  - OpenCL renderer

## [0.2.5] - 2022-08-14

### Added

  - Texture LOD selection using ray cones

### Changed

  - Nightly tests are set up to check all CPU archs (SSE/AVX etc.)
  - JPG images are decoded using turbojpeg lib

## [0.2.0] - 2022-08-12

### Added

  - Resurrected SIMD renderer (single ray vs 8 bboxes)
  - Directional light tests

### Fixed

  - Square artifacts in SIMD renderer
  - Black stripes on uneven resolutions
  - Failing alpha material tests

### Changed

  - BVH construction uses binning now for significant speedup
  - Shadow rays are traced in seperate stage

[Unreleased]: https://gitlab.com/sergcpp/raydemo/-/compare/v0.4.5...master
[0.4.5]: https://gitlab.com/sergcpp/raydemo/-/releases/v0.4.5
[0.4.0]: https://gitlab.com/sergcpp/raydemo/-/releases/v0.4.0
[0.3.5]: https://gitlab.com/sergcpp/raydemo/-/releases/v0.3.5
[0.3.0]: https://gitlab.com/sergcpp/raydemo/-/releases/v0.3.0
[0.2.5]: https://gitlab.com/sergcpp/raydemo/-/releases/v0.2.5
[0.2.0]: https://gitlab.com/sergcpp/raydemo/-/releases/v0.2.0
