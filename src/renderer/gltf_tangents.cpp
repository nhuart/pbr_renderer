#include "renderer/gltf_tangents.hpp"

#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <glm/gtx/hash.hpp>
#include <mikktspace.h>

namespace {

struct TangentVertex {
    uint32_t sourceIndex;
    glm::vec4 tangent;

    bool operator==(TangentVertex const&) const = default;
};

struct TangentVertexHash {
    size_t operator()(TangentVertex const& vertex) const {
        return std::hash<uint32_t>{}(vertex.sourceIndex) ^
               (std::hash<glm::vec4>{}(vertex.tangent) << 1);
    }
};

class TangentSeamBuilder {
public:
    explicit TangentSeamBuilder(size_t vertexCount)
            : mHasTangent(vertexCount, false) {
        mRemap.sources.resize(vertexCount);
        std::iota(mRemap.sources.begin(), mRemap.sources.end(), 0);
        mRemap.tangents.resize(vertexCount, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }

    gltf::TangentVertexRemap build(std::span<uint32_t const> indices,
            std::span<glm::vec4 const> cornerTangents) {
        mRemap.indices.reserve(indices.size());
        for (size_t corner = 0; corner < indices.size(); ++corner) {
            mRemap.indices.push_back(indexFor(indices[corner], cornerTangents[corner]));
        }
        return std::move(mRemap);
    }

private:
    uint32_t indexFor(uint32_t sourceIndex, glm::vec4 const& tangent) {
        if (!mHasTangent.at(sourceIndex)) {
            mRemap.tangents[sourceIndex] = tangent;
            mHasTangent[sourceIndex] = true;
            return sourceIndex;
        }
        if (mRemap.tangents[sourceIndex] == tangent) {
            return sourceIndex;
        }
        return splitIndexFor(sourceIndex, tangent);
    }

    uint32_t splitIndexFor(uint32_t sourceIndex, glm::vec4 const& tangent) {
        auto [entry, inserted] = mSplitVertices.try_emplace(TangentVertex{ sourceIndex, tangent },
                static_cast<uint32_t>(mRemap.sources.size()));
        if (inserted) {
            mRemap.sources.push_back(sourceIndex);
            mRemap.tangents.push_back(tangent);
        }
        return entry->second;
    }

    gltf::TangentVertexRemap mRemap;
    std::vector<bool> mHasTangent;
    std::unordered_map<TangentVertex, uint32_t, TangentVertexHash> mSplitVertices;
};

struct TangentMesh {
    std::span<Vertex const> vertices;
    std::span<uint32_t const> indices;
    std::vector<glm::vec4> tangents;

    static TangentMesh& fromContext(SMikkTSpaceContext const* context) {
        return *static_cast<TangentMesh*>(context->m_pUserData);
    }

    static size_t cornerIndex(int face, int corner) {
        return static_cast<size_t>(face) * 3 + static_cast<size_t>(corner);
    }

    Vertex const& vertex(int face, int corner) const {
        return vertices[indices[cornerIndex(face, corner)]];
    }
};

int faceCount(SMikkTSpaceContext const* context) {
    return static_cast<int>(TangentMesh::fromContext(context).indices.size() / 3);
}

int faceVertexCount(SMikkTSpaceContext const*, int) { return 3; }

void readPosition(SMikkTSpaceContext const* context, float output[], int face, int corner) {
    auto const& position = TangentMesh::fromContext(context).vertex(face, corner).pos;
    output[0] = position.x;
    output[1] = position.y;
    output[2] = position.z;
}

void readNormal(SMikkTSpaceContext const* context, float output[], int face, int corner) {
    auto const& normal = TangentMesh::fromContext(context).vertex(face, corner).normal;
    output[0] = normal.x;
    output[1] = normal.y;
    output[2] = normal.z;
}

void readTexCoord(SMikkTSpaceContext const* context, float output[], int face, int corner) {
    auto const& uv = TangentMesh::fromContext(context).vertex(face, corner).normalTexCoord;
    output[0] = uv.x;
    output[1] = uv.y;
}

void storeTangent(SMikkTSpaceContext const* context, float const tangent[], float handedness,
        int face, int corner) {
    auto& mesh = TangentMesh::fromContext(context);
    mesh.tangents[TangentMesh::cornerIndex(face, corner)] =
            glm::vec4(tangent[0], tangent[1], tangent[2], handedness);
}

void validateTriangleIndices(std::span<Vertex const> vertices, std::span<uint32_t const> indices) {
    if (indices.size() % 3 != 0 || indices.size() > std::numeric_limits<int>::max()) {
        throw std::runtime_error("MikkTSpace requires an indexed triangle list within int limits");
    }
    for (uint32_t index: indices) {
        if (index >= vertices.size()) {
            throw std::runtime_error("MikkTSpace vertex index is out of range");
        }
    }
}

SMikkTSpaceInterface tangentCallbacks() {
    return { .m_getNumFaces = faceCount,
        .m_getNumVerticesOfFace = faceVertexCount,
        .m_getPosition = readPosition,
        .m_getNormal = readNormal,
        .m_getTexCoord = readTexCoord,
        .m_setTSpaceBasic = storeTangent,
        .m_setTSpace = nullptr };
}

} // namespace

gltf::TangentVertexRemap gltf::splitTangentSeams(size_t vertexCount,
        std::span<uint32_t const> indices, std::span<glm::vec4 const> cornerTangents) {
    return TangentSeamBuilder(vertexCount).build(indices, cornerTangents);
}

std::vector<glm::vec4> gltf::generateTangents(std::span<Vertex const> vertices,
        std::span<uint32_t const> indices) {
    if (indices.empty()) {
        return {};
    }
    validateTriangleIndices(vertices, indices);
    TangentMesh mesh{ vertices, indices, std::vector<glm::vec4>(indices.size()) };
    auto callbacks = tangentCallbacks();
    SMikkTSpaceContext context{ &callbacks, &mesh };
    if (!genTangSpaceDefault(&context)) {
        throw std::runtime_error("MikkTSpace tangent generation failed");
    }
    return std::move(mesh.tangents);
}
