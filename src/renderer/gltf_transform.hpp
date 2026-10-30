#pragma once

#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

#include "renderer/vertex.hpp"

namespace gltf {

inline glm::mat4 localTransform(tinygltf::Node const& node) {
    if (node.matrix.size() == 16) {
        return glm::mat4(glm::make_mat4(node.matrix.data()));
    }

    glm::mat4 transform(1.0f);
    if (node.translation.size() == 3) {
        transform = glm::translate(transform, glm::vec3(glm::make_vec3(node.translation.data())));
    }
    if (node.rotation.size() == 4) {
        transform *= glm::mat4_cast(
                glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    }
    if (node.scale.size() == 3) {
        transform = glm::scale(transform, glm::vec3(glm::make_vec3(node.scale.data())));
    }
    return transform;
}

struct MeshInstance {
    int mesh;
    glm::mat4 transform;
};

inline void appendMeshInstances(tinygltf::Model const& model, int nodeIndex,
        glm::mat4 const& parentTransform, std::vector<MeshInstance>& instances) {
    auto const& node = model.nodes.at(nodeIndex);
    glm::mat4 transform = parentTransform * localTransform(node);
    if (node.mesh >= 0) {
        instances.push_back({ node.mesh, transform });
    }
    for (int child: node.children) {
        appendMeshInstances(model, child, transform, instances);
    }
}

inline std::vector<int> rootNodeIndices(tinygltf::Model const& model) {
    if (!model.scenes.empty()) {
        int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        return model.scenes.at(sceneIndex).nodes;
    }

    // Assets without a scene still need hierarchy traversal, rather than loading each mesh once.
    std::vector<bool> isChild(model.nodes.size(), false);
    for (auto const& node: model.nodes) {
        for (int child: node.children) {
            isChild.at(child) = true;
        }
    }
    std::vector<int> roots;
    for (size_t root = 0; root < model.nodes.size(); ++root) {
        if (!isChild[root]) {
            roots.push_back(static_cast<int>(root));
        }
    }
    return roots;
}

inline std::vector<MeshInstance> meshInstances(tinygltf::Model const& model) {
    std::vector<MeshInstance> instances;
    for (int root: rootNodeIndices(model)) {
        appendMeshInstances(model, root, glm::mat4(1.0f), instances);
    }
    return instances;
}

class VertexTransform {
public:
    explicit VertexTransform(glm::mat4 const& transform)
            : mTransform(transform),
              mLinear(transform),
              mDeterminant(glm::determinant(mLinear)),
              mNormal(mDeterminant != 0.0f ? glm::transpose(glm::inverse(mLinear))
                                           : glm::mat3(0.0f)) {}

    [[nodiscard]] bool reversesWinding() const { return mDeterminant < 0.0f; }

    void apply(Vertex& vertex) const {
        vertex.pos = glm::vec3(mTransform * glm::vec4(vertex.pos, 1.0f));
        vertex.normal = normalizeOrZero(mNormal * vertex.normal);
        glm::vec3 tangent = mLinear * glm::vec3(vertex.tangent);
        tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
        vertex.tangent = glm::vec4(normalizeOrZero(tangent),
                vertex.tangent.w * (reversesWinding() ? -1.0f : 1.0f));
    }

private:
    static glm::vec3 normalizeOrZero(glm::vec3 const& direction) {
        float lengthSquared = glm::dot(direction, direction);
        return lengthSquared > 0.0f ? direction / glm::sqrt(lengthSquared) : glm::vec3(0.0f);
    }

    glm::mat4 mTransform;
    glm::mat3 mLinear;
    float mDeterminant;
    glm::mat3 mNormal;
};

} // namespace gltf
