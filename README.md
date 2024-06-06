# MillRenderer

My hobby renderer.
The development is in progress.

At default setting, it renders Sponza.
![Sponza Screen Shot](https://github.com/monguri/MillRenderer/blob/main/Sponza.png "Sponza Screen Shot")

## Characteristics
- First version is based on and refered sample projects of ProjectAsura.
  - https://gihyo.jp/book/2021/978-4-297-12365-9/support
- Currently, I concentrate on developing various rendering passes and expressions.
  - I postpone applying various optimizing techniques. Big techniques that ZPrePass, deferred rendering or some other thing, and smaller techniques.
- D3D12 application.
- Multi render target base pass. (Scene color, depth, normal).
- Forward renderer so far.
- Direcitonal light with shadow map.
- Spot lights with shadow map.
- Point lights without shadow map.
- Volumetric Fog
- Postprocess
  - SSAO
  - SSR
  - Temporal AA (Optionary FXAA)
  - Bloom
  - Motion Blur
  - Tonemap

## About some assets license
Assets are from https://github.com/monguri/glTF-Sample-Viewer-Release.
About their licenses, look that repositry. 

## How to execute and operate.
Execute Sample\project\x64\[Debug, Release]\Sample.exe
You can operate camera by mouse.

## How to build
You must get project by git clone --recursive.
Downloaded zip does not include submodule. 
Build Sample\project\Sample.sln with Visual Studio 2022.

## Future plan.
- SSGI
- Deferred rendering.
- Sky and cloud.
- Visibility map.
- Some other optimizing techiniques.
- Vulkan support.
- etc.
