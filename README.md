# MillRenderer

My hobby renderer.
The development is in progress.

At default setting, it renders Sponza.
![Sponza Screen Shot](https://github.com/monguri/MillRenderer/blob/main/Sponza.png "Sponza Screen Shot")

- First version is based on and refered D3D12Samples of ProjectAsura.
  - https://github.com/ProjectAsura/D3D12Samples
- Currently, I concentrate on developing various rendering passes and expressions. I postpone applying various optimizing techniques.
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

Assets are from https://github.com/monguri/glTF-Sample-Viewer-Release.
About their licenses, look that repositry. 
