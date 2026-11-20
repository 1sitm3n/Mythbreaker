# Third-party notices

Mythbreaker uses the libraries below. Their licenses remain in force independently
of the engine's MIT license. Source headers retain additional upstream notices.

## Libraries under the MIT license

- Dear ImGui 1.91.9b docking: Copyright (c) 2014–2025 Omar Cornut.
  Source: https://github.com/ocornut/imgui (commit 4806a1924ff6181180bf5e4b8b79ab4394118875).
- GLM 1.0.2: Copyright (c) 2005 G-Truc Creation. MIT option selected.
  Source: https://github.com/g-truc/glm.
- Vulkan Memory Allocator 3.3.0: Copyright (c) 2017–2025 Advanced Micro Devices, Inc. All rights reserved.
  Source: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.
- TinyGLTF 2.9: Copyright (c) 2015–Present Syoyo Fujita, Aurélien Chatelain and many contributors.
  Source: https://github.com/syoyo/tinygltf.
- tinyobjloader: Copyright (c) 2012–Present Syoyo Fujita and many contributors.
  Source: https://github.com/tinyobjloader/tinyobjloader.
- nlohmann/json: Copyright (c) 2013–2025 Niels Lohmann.
  Additional embedded notices include Evan Nemerson, the Abseil Authors,
  Björn Hoehrmann and Florian Loitsch; see third_party/nlohmann/json.hpp.
  Source: https://github.com/nlohmann/json.
- stb_image and stb_image_write: Copyright (c) 2017 Sean Barrett. MIT option selected.
  Source: https://github.com/nothings/stb.

The following permission and disclaimer applies to each MIT component above:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## GLFW 3.4 — zlib license

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Löwy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.


## miniaudio 0.11.23 — MIT No Attribution

Source: https://github.com/mackron/miniaudio. MIT-0 option selected.

Copyright 2025 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## TinyGLTF embedded Boost-licensed helpers

Copyright (C) 2003 Davis E. King.

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:
The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

## Model assets

The engine's primitives and starter scene layout are authored in this repository.
The following sample model files were already present in the RPG prototype:

- **Fox.glb**: model by PixelMannen (2014, CC0); rigging and animation by
  tomkranis (2014, CC BY 4.0); glTF conversion by AsoboStudio and scurest
  (2017, CC BY 4.0). The binary is unmodified; the sample scene changes only
  its placement, scale and selected animation.
  https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox
  https://creativecommons.org/licenses/by/4.0/
  https://creativecommons.org/publicdomain/zero/1.0/
- **Duck.glb**: Copyright 2006 Sony, SCEA Shared Source License 1.0.
  https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Duck
  The complete accompanying notice/license is in assets/models/Duck.NOTICE.md
  and assets/models/SCEA-LICENSE.txt. This model remains an importer regression
  fixture and is not included in the starter game package.

The Fox attribution is also shipped inside the starter project's Assets directory
so that a project export preserves it. Existing legacy demo media outside the
starter project is not copied into new projects or standalone packages.

## Platform dependencies

Vulkan is a registered trademark of the Khronos Group Inc. Vulkan loader and
graphics drivers are provided by the operating system/driver installation.
The engine does not redistribute a GPU driver or the Vulkan SDK. A build made
with MinGW may need its compiler runtime DLLs and their separate notices;
MSVC release builds use the static C/C++ runtime by default.
