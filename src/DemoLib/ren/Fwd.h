#pragma once

#include "Storage.h"

namespace Ren {
class Anim;
class Buffer;
class Camera;
class Material;
class Mesh;
class Program;
class Texture2D;

typedef StorageRef<Anim> AnimRef;
typedef StorageRef<Buffer> BufferRef;
typedef StorageRef<Material> MaterialRef;
typedef StorageRef<Mesh> MeshRef;
typedef StorageRef<Program> ProgramRef;
typedef StorageRef<Texture2D> Texture2DRef;

#if defined(USE_GL_RENDER)
void CheckError(const char *op = "undefined");
#endif
}