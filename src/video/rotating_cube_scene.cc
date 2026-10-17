#include "rotating_cube_scene.h"

#include "frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if ENGINE_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace video {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct RgbColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Face {
    int a = 0;
    int b = 0;
    int c = 0;
};

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<Face> faces;
    bool loaded_with_assimp = false;
};

struct ProjectedVertex {
    Vec3 world;
    Vec2 screen;
    float depth = 1.0f;
};

struct DrawFace {
    Face face;
    float depth = 0.0f;
    RgbColor color;
    uint8_t alpha = 80;
};

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Vec3 Add(Vec3 a, Vec3 b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Sub(Vec3 a, Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Scale(Vec3 value, float amount) {
    return Vec3{value.x * amount, value.y * amount, value.z * amount};
}

float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b) {
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 Normalize(Vec3 value) {
    const float length = std::sqrt(std::max(1.0e-12f, Dot(value, value)));
    return Scale(value, 1.0f / length);
}

RgbColor Mix(RgbColor a, RgbColor b, float amount) {
    amount = Clamp01(amount);
    const float inverse = 1.0f - amount;
    return RgbColor{
        static_cast<uint8_t>(std::clamp(a.r * inverse + b.r * amount, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(a.g * inverse + b.g * amount, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(a.b * inverse + b.b * amount, 0.0f, 255.0f)),
    };
}

void BlendPixel(Frame& frame, int x, int y, RgbColor color, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= frame.Width() || y >= frame.Height() || alpha == 0) return;
    uint8_t* pixel = frame.Data() + (static_cast<size_t>(y) * frame.Width() + x) * 4u;
    const float amount = static_cast<float>(alpha) / 255.0f;
    const float inverse = 1.0f - amount;
    pixel[0] = static_cast<uint8_t>(std::clamp(pixel[0] * inverse + color.b * amount, 0.0f, 255.0f));
    pixel[1] = static_cast<uint8_t>(std::clamp(pixel[1] * inverse + color.g * amount, 0.0f, 255.0f));
    pixel[2] = static_cast<uint8_t>(std::clamp(pixel[2] * inverse + color.r * amount, 0.0f, 255.0f));
    pixel[3] = 255;
}

void FillSoftCircle(Frame& frame, int center_x, int center_y, float radius, RgbColor color, uint8_t alpha) {
    if (radius <= 0.0f) return;
    const int min_x = std::max(0, static_cast<int>(std::floor(center_x - radius)));
    const int max_x = std::min(frame.Width() - 1, static_cast<int>(std::ceil(center_x + radius)));
    const int min_y = std::max(0, static_cast<int>(std::floor(center_y - radius)));
    const int max_y = std::min(frame.Height() - 1, static_cast<int>(std::ceil(center_y + radius)));
    const float radius_sq = radius * radius;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x - center_x);
            const float dy = static_cast<float>(y - center_y);
            const float distance_sq = dx * dx + dy * dy;
            if (distance_sq > radius_sq) continue;
            const float falloff = 1.0f - std::sqrt(distance_sq / radius_sq);
            BlendPixel(frame, x, y, color, static_cast<uint8_t>(alpha * falloff * falloff));
        }
    }
}

float Edge(Vec2 a, Vec2 b, Vec2 p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

void FillTriangle(Frame& frame, Vec2 p0, Vec2 p1, Vec2 p2, RgbColor color, uint8_t alpha) {
    const float area = Edge(p0, p1, p2);
    if (std::abs(area) < 0.0001f) return;

    const int min_x = std::max(0, static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))));
    const int max_x = std::min(frame.Width() - 1, static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))));
    const int max_y = std::min(frame.Height() - 1, static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const float w0 = Edge(p1, p2, p);
            const float w1 = Edge(p2, p0, p);
            const float w2 = Edge(p0, p1, p);
            if ((area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                || (area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)) {
                BlendPixel(frame, x, y, color, alpha);
            }
        }
    }
}

float DistanceToSegment(Vec2 point, Vec2 a, Vec2 b) {
    const float ab_x = b.x - a.x;
    const float ab_y = b.y - a.y;
    const float len_sq = ab_x * ab_x + ab_y * ab_y;
    if (len_sq <= 0.0001f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    const float t = std::clamp(((point.x - a.x) * ab_x + (point.y - a.y) * ab_y) / len_sq, 0.0f, 1.0f);
    const float x = a.x + ab_x * t;
    const float y = a.y + ab_y * t;
    const float dx = point.x - x;
    const float dy = point.y - y;
    return std::sqrt(dx * dx + dy * dy);
}

void DrawLine(Frame& frame, Vec2 a, Vec2 b, float width, RgbColor color, uint8_t alpha) {
    const int radius = std::max(1, static_cast<int>(std::ceil(width * 0.5f)));
    const int min_x = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x))) - radius);
    const int max_x = std::min(frame.Width() - 1, static_cast<int>(std::ceil(std::max(a.x, b.x))) + radius);
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y))) - radius);
    const int max_y = std::min(frame.Height() - 1, static_cast<int>(std::ceil(std::max(a.y, b.y))) + radius);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float distance = DistanceToSegment(Vec2{static_cast<float>(x), static_cast<float>(y)}, a, b);
            if (distance > width * 0.5f) continue;
            const float coverage = 1.0f - Clamp01(distance / (width * 0.5f));
            BlendPixel(frame, x, y, color, static_cast<uint8_t>(alpha * std::sqrt(coverage)));
        }
    }
}

void DrawNeonLine(Frame& frame, Vec2 a, Vec2 b, RgbColor color, float intensity) {
    intensity = Clamp01(intensity);
    DrawLine(frame, Vec2{a.x - 2.8f, a.y + 1.2f}, Vec2{b.x - 2.8f, b.y + 1.2f}, 42.0f, RgbColor{255, 25, 8}, static_cast<uint8_t>(8 + 30 * intensity));
    DrawLine(frame, Vec2{a.x + 2.6f, a.y - 1.6f}, Vec2{b.x + 2.6f, b.y - 1.6f}, 40.0f, RgbColor{58, 44, 255}, static_cast<uint8_t>(8 + 30 * intensity));
    DrawLine(frame, Vec2{a.x + 0.4f, a.y + 2.4f}, Vec2{b.x + 0.4f, b.y + 2.4f}, 30.0f, RgbColor{206, 24, 255}, static_cast<uint8_t>(7 + 24 * intensity));
    DrawLine(frame, a, b, 13.5f, color, static_cast<uint8_t>(30 + 58 * intensity));
    DrawLine(frame, a, b, 5.4f, Mix(color, RgbColor{255, 255, 255}, 0.18f), static_cast<uint8_t>(132 + 72 * intensity));
    DrawLine(frame, a, b, 1.35f, Mix(color, RgbColor{255, 240, 255}, 0.34f), static_cast<uint8_t>(188 + 44 * intensity));
}

void NormalizeMesh(Mesh* mesh) {
    if (mesh == nullptr || mesh->vertices.empty()) return;
    Vec3 min_v{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max_v{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (Vec3 vertex : mesh->vertices) {
        min_v.x = std::min(min_v.x, vertex.x);
        min_v.y = std::min(min_v.y, vertex.y);
        min_v.z = std::min(min_v.z, vertex.z);
        max_v.x = std::max(max_v.x, vertex.x);
        max_v.y = std::max(max_v.y, vertex.y);
        max_v.z = std::max(max_v.z, vertex.z);
    }
    const Vec3 center = Scale(Add(min_v, max_v), 0.5f);
    const float extent = std::max({max_v.x - min_v.x, max_v.y - min_v.y, max_v.z - min_v.z, 0.0001f});
    const float scale = 2.0f / extent;
    for (Vec3& vertex : mesh->vertices) {
        vertex = Scale(Sub(vertex, center), scale);
    }
}

Mesh MakeFallbackCubeMesh() {
    Mesh mesh;
    mesh.vertices = {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
    };
    mesh.faces = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
        {1, 5, 6}, {1, 6, 2}, {2, 6, 7}, {2, 7, 3}, {3, 7, 4}, {3, 4, 0},
    };
    return mesh;
}

Mesh LoadCubeMesh() {
#if ENGINE_HAS_ASSIMP
    static constexpr char kCubeObj[] =
        "o DarkXRotatingCube\n"
        "v -1 -1 -1\n"
        "v 1 -1 -1\n"
        "v 1 1 -1\n"
        "v -1 1 -1\n"
        "v -1 -1 1\n"
        "v 1 -1 1\n"
        "v 1 1 1\n"
        "v -1 1 1\n"
        "f 1 2 3 4\n"
        "f 5 8 7 6\n"
        "f 1 5 6 2\n"
        "f 2 6 7 3\n"
        "f 3 7 8 4\n"
        "f 4 8 5 1\n";
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(
        kCubeObj,
        std::strlen(kCubeObj),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals,
        "obj");
    if (scene != nullptr && scene->HasMeshes()) {
        Mesh mesh;
        mesh.loaded_with_assimp = true;
        for (unsigned mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
            const aiMesh* ai_mesh = scene->mMeshes[mesh_index];
            if (ai_mesh == nullptr || !ai_mesh->HasPositions()) continue;
            const int vertex_offset = static_cast<int>(mesh.vertices.size());
            for (unsigned vertex_index = 0; vertex_index < ai_mesh->mNumVertices; ++vertex_index) {
                const aiVector3D& v = ai_mesh->mVertices[vertex_index];
                mesh.vertices.push_back(Vec3{v.x, v.y, v.z});
            }
            for (unsigned face_index = 0; face_index < ai_mesh->mNumFaces; ++face_index) {
                const aiFace& face = ai_mesh->mFaces[face_index];
                if (face.mNumIndices != 3) continue;
                mesh.faces.push_back(Face{
                    vertex_offset + static_cast<int>(face.mIndices[0]),
                    vertex_offset + static_cast<int>(face.mIndices[1]),
                    vertex_offset + static_cast<int>(face.mIndices[2]),
                });
            }
        }
        if (!mesh.vertices.empty() && !mesh.faces.empty()) {
            NormalizeMesh(&mesh);
            return mesh;
        }
    }
#endif
    return MakeFallbackCubeMesh();
}

const Mesh& CubeMesh() {
    static const Mesh mesh = LoadCubeMesh();
    return mesh;
}

Vec3 Rotate(Vec3 value, float rx, float ry, float rz) {
    const float cx = std::cos(rx);
    const float sx = std::sin(rx);
    const float cy = std::cos(ry);
    const float sy = std::sin(ry);
    const float cz = std::cos(rz);
    const float sz = std::sin(rz);

    value = Vec3{value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx};
    value = Vec3{value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy};
    value = Vec3{value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z};
    return value;
}

ProjectedVertex Project(Vec3 local, const RotatingCubeSceneOptions& options, const Frame& frame) {
    const float pulse = 1.0f + options.bass * 0.12f + options.rms * 0.05f;
    const double time = options.time_seconds;
    Vec3 world = Scale(local, pulse);
    world = Rotate(
        world,
        0.70f + static_cast<float>(time * 0.46),
        0.66f + static_cast<float>(time * 0.82),
        -0.58f + 0.10f * std::sin(static_cast<float>(time) * 0.9f));

    const float camera_distance = 4.05f;
    const float depth = std::max(0.2f, camera_distance + world.z);
    const float focal = static_cast<float>(std::min(frame.Width(), frame.Height())) * 1.30f;
    const float center_x = static_cast<float>(frame.Width()) * 0.5f;
    const float center_y = static_cast<float>(frame.Height()) * 0.48f;
    return ProjectedVertex{
        world,
        Vec2{center_x + world.x * focal / depth, center_y - world.y * focal / depth},
        depth,
    };
}

RgbColor FaceColor(Vec3 normal, int face_index, const RotatingCubeSceneOptions& options) {
    const RgbColor red{124, 13, 12};
    const RgbColor amber{148, 58, 10};
    const RgbColor violet{58, 10, 118};
    const RgbColor blue{12, 18, 98};
    const RgbColor deep{0, 0, 5};

    RgbColor accent = (face_index % 3 == 0) ? red : ((face_index % 3 == 1) ? violet : blue);
    if (normal.x < -0.35f) accent = Mix(red, amber, 0.35f + options.bass * 0.35f);
    if (normal.x > 0.35f) accent = Mix(violet, blue, 0.35f + options.high * 0.45f);
    if (normal.y > 0.45f) accent = Mix(violet, RgbColor{255, 88, 195}, 0.25f + options.mid * 0.30f);

    const float light = Clamp01(0.10f + Dot(Normalize(Vec3{-0.34f, 0.62f, 0.70f}), normal) * 0.30f);
    return Mix(deep, accent, 0.08f + light * 0.28f);
}

void DrawMesh(Frame& frame, const RotatingCubeSceneOptions& options, const std::vector<ProjectedVertex>& projected) {
    const Mesh& mesh = CubeMesh();
    std::vector<DrawFace> draw_faces;
    draw_faces.reserve(mesh.faces.size());
    for (size_t index = 0; index < mesh.faces.size(); ++index) {
        const Face& face = mesh.faces[index];
        if (face.a < 0 || face.b < 0 || face.c < 0
            || face.a >= static_cast<int>(projected.size())
            || face.b >= static_cast<int>(projected.size())
            || face.c >= static_cast<int>(projected.size())) {
            continue;
        }
        const Vec3 ab = Sub(projected[face.b].world, projected[face.a].world);
        const Vec3 ac = Sub(projected[face.c].world, projected[face.a].world);
        const Vec3 normal = Normalize(Cross(ab, ac));
        const float depth = (projected[face.a].depth + projected[face.b].depth + projected[face.c].depth) / 3.0f;
        draw_faces.push_back(DrawFace{
            face,
            depth,
            FaceColor(normal, static_cast<int>(index), options),
            static_cast<uint8_t>(18 + 30 * Clamp01(options.rms + options.bass * 0.28f)),
        });
    }
    std::sort(draw_faces.begin(), draw_faces.end(), [](const DrawFace& a, const DrawFace& b) {
        return a.depth > b.depth;
    });
    for (const DrawFace& item : draw_faces) {
        FillTriangle(
            frame,
            projected[item.face.a].screen,
            projected[item.face.b].screen,
            projected[item.face.c].screen,
            item.color,
            item.alpha);
    }
}

void DrawCubeEdges(Frame& frame, const RotatingCubeSceneOptions& options, const std::vector<ProjectedVertex>& projected) {
    static constexpr std::array<std::array<int, 2>, 12> kEdges{{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};
    const float intensity = Clamp01(0.34f + options.bass * 0.36f + options.high * 0.18f);
    for (const auto& edge : kEdges) {
        if (edge[0] >= static_cast<int>(projected.size()) || edge[1] >= static_cast<int>(projected.size())) continue;
        const float side = (projected[edge[0]].world.x + projected[edge[1]].world.x) * 0.5f;
        const RgbColor color = side < -0.18f
            ? Mix(RgbColor{242, 33, 22}, RgbColor{255, 106, 22}, 0.20f + options.bass * 0.35f)
            : Mix(RgbColor{154, 30, 255}, RgbColor{45, 60, 255}, 0.40f + options.high * 0.40f);
        DrawNeonLine(frame, projected[edge[0]].screen, projected[edge[1]].screen, color, intensity);
    }
}

void DrawTopRibbon(Frame& frame, const RotatingCubeSceneOptions& options) {
    std::vector<Vec2> points;
    std::vector<RgbColor> colors;
    points.reserve(72);
    colors.reserve(72);
    const float phase = static_cast<float>(options.time_seconds) * 0.34f;
    for (int i = 0; i < 72; ++i) {
        const float t = static_cast<float>(i) / 71.0f;
        const float angle = -2.42f + t * 3.08f + phase * 0.14f;
        const Vec3 local{
            1.08f * std::cos(angle),
            1.02f + 0.07f * std::sin(t * kPi),
            0.58f * std::sin(angle) + 0.07f * std::sin(phase + t * kPi),
        };
        ProjectedVertex projected = Project(local, options, frame);
        points.push_back(projected.screen);
        colors.push_back(Mix(RgbColor{255, 42, 12}, RgbColor{126, 56, 255}, t));
    }
    for (size_t index = 1; index < points.size(); ++index) {
        DrawNeonLine(frame, points[index - 1u], points[index], colors[index], 0.92f + options.bass * 0.08f);
    }
}

}  // namespace

bool RotatingCubeSceneUsesAssimp() {
    return CubeMesh().loaded_with_assimp;
}

void RenderRotatingCubeScene(Frame& frame, const RotatingCubeSceneOptions& options) {
    if (!frame.IsValid() || frame.Format() != PixelFormat::BGRA8) return;
    const int center_x = frame.Width() / 2;
    const int center_y = static_cast<int>(frame.Height() * 0.44f);
    const float bass = Clamp01(options.bass);
    const float high = Clamp01(options.high);
    FillSoftCircle(frame, center_x, center_y, frame.Height() * (0.27f + bass * 0.05f), RgbColor{18, 10, 58}, 22);
    FillSoftCircle(frame, center_x - frame.Width() / 9, center_y + frame.Height() / 10, frame.Height() * 0.19f, RgbColor{120, 13, 7}, static_cast<uint8_t>(18 + bass * 22));
    FillSoftCircle(frame, center_x + frame.Width() / 7, center_y - frame.Height() / 18, frame.Height() * 0.18f, RgbColor{18, 18, 118}, static_cast<uint8_t>(16 + high * 24));

    const Mesh& mesh = CubeMesh();
    std::vector<ProjectedVertex> projected;
    projected.reserve(mesh.vertices.size());
    for (Vec3 vertex : mesh.vertices) {
        projected.push_back(Project(vertex, options, frame));
    }

    DrawMesh(frame, options, projected);
    DrawTopRibbon(frame, options);
    DrawCubeEdges(frame, options, projected);
}

}  // namespace video