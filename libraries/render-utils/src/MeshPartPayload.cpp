//
//  MeshPartPayload.cpp
//  interface/src/renderer
//
//  Created by Sam Gateau on 10/3/15.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MeshPartPayload.h"

#include <PerfStat.h>
#include <DualQuaternion.h>

#include "DeferredLightingEffect.h"

#include "RenderPipelines.h"

using namespace render;

namespace render {
template <> const ItemKey payloadGetKey(const MeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getKey();
    }
    return ItemKey::Builder::opaqueShape(); // for lack of a better idea
}

template <> const Item::Bound payloadGetBound(const MeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getBound();
    }
    return Item::Bound();
}

template <> const ShapeKey shapeGetShapeKey(const MeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getShapeKey();
    }
    return ShapeKey::Builder::invalid();
}

template <> void payloadRender(const MeshPartPayload::Pointer& payload, RenderArgs* args) {
    return payload->render(args);
}
}

MeshPartPayload::MeshPartPayload(const std::shared_ptr<const graphics::Mesh>& mesh, int partIndex, graphics::MaterialPointer material) {
    updateMeshPart(mesh, partIndex);
    addMaterial(graphics::MaterialLayer(material, 0));
}

void MeshPartPayload::updateMeshPart(const std::shared_ptr<const graphics::Mesh>& drawMesh, int partIndex) {
    _drawMesh = drawMesh;
    if (_drawMesh) {
        auto vertexFormat = _drawMesh->getVertexFormat();
        _hasColorAttrib = vertexFormat->hasAttribute(gpu::Stream::COLOR);
        _drawPart = _drawMesh->getPartBuffer().get<graphics::Mesh::Part>(partIndex);
        _localBound = _drawMesh->evalPartBound(partIndex);
    }
}

void MeshPartPayload::updateTransform(const Transform& transform, const Transform& offsetTransform) {
    _transform = transform;
    Transform::mult(_drawTransform, _transform, offsetTransform);
    _worldBound = _localBound;
    _worldBound.transform(_drawTransform);
}

void MeshPartPayload::addMaterial(graphics::MaterialLayer material) {
    _drawMaterials.push(material);
}

void MeshPartPayload::removeMaterial(graphics::MaterialPointer material) {
    _drawMaterials.remove(material);
}

void MeshPartPayload::updateKey(bool isVisible, bool isLayered, bool canCastShadow, uint8_t tagBits, bool isGroupCulled) {
    ItemKey::Builder builder;
    builder.withTypeShape();

    if (!isVisible) {
        builder.withInvisible();
    }

    builder.withTagBits(tagBits);

    if (isLayered) {
        builder.withLayered();
    }

    if (canCastShadow) {
        builder.withShadowCaster();
    }

    if (isGroupCulled) {
        builder.withSubMetaCulled();
    }

    if (_drawMaterials.top().material) {
        auto matKey = _drawMaterials.top().material->getKey();
        if (matKey.isTranslucent()) {
            builder.withTransparent();
        }
    }

    _itemKey = builder.build();
}

ItemKey MeshPartPayload::getKey() const {
    return _itemKey;
}

Item::Bound MeshPartPayload::getBound() const {
    return _worldBound;
}

ShapeKey MeshPartPayload::getShapeKey() const {
    graphics::MaterialKey drawMaterialKey;
    if (_drawMaterials.top().material) {
        drawMaterialKey = _drawMaterials.top().material->getKey();
    }

    ShapeKey::Builder builder;
    builder.withMaterial();

    if (drawMaterialKey.isTranslucent()) {
        builder.withTranslucent();
    }
    if (drawMaterialKey.isNormalMap()) {
        builder.withTangents();
    }
    if (drawMaterialKey.isMetallicMap()) {
        builder.withSpecular();
    }
    if (drawMaterialKey.isLightmapMap()) {
        builder.withLightmap();
    }
    return builder.build();
}

void MeshPartPayload::drawCall(gpu::Batch& batch) const {
    batch.drawIndexed(gpu::TRIANGLES, _drawPart._numIndices, _drawPart._startIndex);
}

void MeshPartPayload::bindMesh(gpu::Batch& batch) {
    batch.setIndexBuffer(gpu::UINT32, (_drawMesh->getIndexBuffer()._buffer), 0);

    batch.setInputFormat((_drawMesh->getVertexFormat()));

    batch.setInputStream(0, _drawMesh->getVertexStream());
}

void MeshPartPayload::bindTransform(gpu::Batch& batch, RenderArgs::RenderMode renderMode) const {
    batch.setModelTransform(_drawTransform);
}


void MeshPartPayload::render(RenderArgs* args) {
    PerformanceTimer perfTimer("MeshPartPayload::render");

    if (!args) {
        return;
    }

    gpu::Batch& batch = *(args->_batch);

    // Bind the model transform and the skinCLusterMatrices if needed
    bindTransform(batch, args->_renderMode);

    //Bind the index buffer and vertex buffer and Blend shapes if needed
    bindMesh(batch);

    // apply material properties
    RenderPipelines::bindMaterial(_drawMaterials.top().material, batch, args->_enableTexturing);
    args->_details._materialSwitches++;

    // Draw!
    {
        PerformanceTimer perfTimer("batch.drawIndexed()");
        drawCall(batch);
    }

    const int INDICES_PER_TRIANGLE = 3;
    args->_details._trianglesRendered += _drawPart._numIndices / INDICES_PER_TRIANGLE;
}

namespace render {
template <> const ItemKey payloadGetKey(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getKey();
    }
    return ItemKey::Builder::opaqueShape(); // for lack of a better idea
}

template <> const Item::Bound payloadGetBound(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getBound();
    }
    return Item::Bound();
}
template <> int payloadGetLayer(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getLayer();
    }
    return 0;
}

template <> const ShapeKey shapeGetShapeKey(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getShapeKey();
    }
    return ShapeKey::Builder::invalid();
}

template <> void payloadRender(const ModelMeshPartPayload::Pointer& payload, RenderArgs* args) {
    return payload->render(args);
}

}

ModelMeshPartPayload::ModelMeshPartPayload(ModelPointer model, int meshIndex, int partIndex, int shapeIndex, const Transform& transform, const Transform& offsetTransform) :
    _meshIndex(meshIndex),
    _shapeID(shapeIndex) {

    assert(model && model->isLoaded());
    _blendedVertexBuffer = model->_blendedVertexBuffers[_meshIndex];
    auto& modelMesh = model->getGeometry()->getMeshes().at(_meshIndex);
    const Model::MeshState& state = model->getMeshState(_meshIndex);

    updateMeshPart(modelMesh, partIndex);
    computeAdjustedLocalBound(state.clusterTransforms);

    updateTransform(transform, offsetTransform);
    Transform renderTransform = transform;
    if (state.clusterTransforms.size() == 1) {
#if defined(SKIN_DQ)
        Transform transform(state.clusterTransforms[0].getRotation(),
                            state.clusterTransforms[0].getScale(),
                            state.clusterTransforms[0].getTranslation());
        renderTransform = transform.worldTransform(Transform(transform));
#else
        renderTransform = transform.worldTransform(Transform(state.clusterTransforms[0]));
#endif

    }
    updateTransformForSkinnedMesh(renderTransform, transform);

    initCache(model);
}

void ModelMeshPartPayload::initCache(const ModelPointer& model) {
    if (_drawMesh) {
        auto vertexFormat = _drawMesh->getVertexFormat();
        _hasColorAttrib = vertexFormat->hasAttribute(gpu::Stream::COLOR);
        _isSkinned = vertexFormat->hasAttribute(gpu::Stream::SKIN_CLUSTER_WEIGHT) && vertexFormat->hasAttribute(gpu::Stream::SKIN_CLUSTER_INDEX);

        const FBXGeometry& geometry = model->getFBXGeometry();
        const FBXMesh& mesh = geometry.meshes.at(_meshIndex);

        _isBlendShaped = !mesh.blendshapes.isEmpty();
        _hasTangents = !mesh.tangents.isEmpty();
    }

    auto networkMaterial = model->getGeometry()->getShapeMaterial(_shapeID);
    if (networkMaterial) {
        addMaterial(graphics::MaterialLayer(networkMaterial, 0));
    }
}

void ModelMeshPartPayload::notifyLocationChanged() {

}

void ModelMeshPartPayload::updateClusterBuffer(const std::vector<TransformType>& clusterTransforms) {
    // Once computed the cluster matrices, update the buffer(s)
    if (clusterTransforms.size() > 1) {
        if (!_clusterBuffer) {
            _clusterBuffer = std::make_shared<gpu::Buffer>(clusterTransforms.size() * sizeof(TransformType),
                (const gpu::Byte*) clusterTransforms.data());
        }
        else {
            _clusterBuffer->setSubData(0, clusterTransforms.size() * sizeof(TransformType),
                (const gpu::Byte*) clusterTransforms.data());
        }
    }
}

void ModelMeshPartPayload::updateTransformForSkinnedMesh(const Transform& renderTransform, const Transform& boundTransform) {
    _transform = renderTransform;
    _worldBound = _adjustedLocalBound;
    _worldBound.transform(boundTransform);
}

// Note that this method is called for models but not for shapes
void ModelMeshPartPayload::updateKey(bool isVisible, bool isLayered, bool canCastShadow, uint8_t tagBits, bool isGroupCulled) {
    ItemKey::Builder builder;
    builder.withTypeShape();

    if (!isVisible) {
        builder.withInvisible();
    }

    builder.withTagBits(tagBits);

    if (isLayered) {
        builder.withLayered();
    }

    if (canCastShadow) {
        builder.withShadowCaster();
    }

    if (isGroupCulled) {
        builder.withSubMetaCulled();
    }

    if (_isBlendShaped || _isSkinned) {
        builder.withDeformed();
    }

    if (_drawMaterials.top().material) {
        auto matKey = _drawMaterials.top().material->getKey();
        if (matKey.isTranslucent()) {
            builder.withTransparent();
        }
    }

    _itemKey = builder.build();
}

void ModelMeshPartPayload::setLayer(bool isLayeredInFront, bool isLayeredInHUD) {
    if (isLayeredInFront) {
        _layer = Item::LAYER_3D_FRONT;
    } else if (isLayeredInHUD) {
        _layer = Item::LAYER_3D_HUD;
    } else {
        _layer = Item::LAYER_3D;
    }
}

int ModelMeshPartPayload::getLayer() const {
    return _layer;
}

void ModelMeshPartPayload::setShapeKey(bool invalidateShapeKey, bool isWireframe) {
    if (invalidateShapeKey) {
        _shapeKey = ShapeKey::Builder::invalid();
        return;
    }

    graphics::MaterialKey drawMaterialKey;
    if (_drawMaterials.top().material) {
        drawMaterialKey = _drawMaterials.top().material->getKey();
    }

    bool isTranslucent = drawMaterialKey.isTranslucent();
    bool hasTangents = drawMaterialKey.isNormalMap() && _hasTangents;
    bool hasSpecular = drawMaterialKey.isMetallicMap();
    bool hasLightmap = drawMaterialKey.isLightmapMap();
    bool isUnlit = drawMaterialKey.isUnlit();

    bool isSkinned = _isSkinned;

    if (isWireframe) {
        isTranslucent = hasTangents = hasSpecular = hasLightmap = isSkinned = false;
    }

    ShapeKey::Builder builder;
    builder.withMaterial();

    if (isTranslucent) {
        builder.withTranslucent();
    }
    if (hasTangents) {
        builder.withTangents();
    }
    if (hasSpecular) {
        builder.withSpecular();
    }
    if (hasLightmap) {
        builder.withLightmap();
    }
    if (isUnlit) {
        builder.withUnlit();
    }
    if (isSkinned) {
        builder.withSkinned();
    }
    if (isWireframe) {
        builder.withWireframe();
    }
    _shapeKey = builder.build();
}

ShapeKey ModelMeshPartPayload::getShapeKey() const {
    return _shapeKey;
}

void ModelMeshPartPayload::bindMesh(gpu::Batch& batch) {
    batch.setIndexBuffer(gpu::UINT32, (_drawMesh->getIndexBuffer()._buffer), 0);
    batch.setInputFormat((_drawMesh->getVertexFormat()));
    if (_isBlendShaped && _blendedVertexBuffer) {
        batch.setInputBuffer(0, _blendedVertexBuffer, 0, sizeof(glm::vec3));
        // Stride is 2*sizeof(glm::vec3) because normal and tangents are interleaved
        batch.setInputBuffer(1, _blendedVertexBuffer, _drawMesh->getNumVertices() * sizeof(glm::vec3), 2 * sizeof(NormalType));
        batch.setInputStream(2, _drawMesh->getVertexStream().makeRangedStream(2));
    } else {
        batch.setInputStream(0, _drawMesh->getVertexStream());
    }
}

void ModelMeshPartPayload::bindTransform(gpu::Batch& batch, RenderArgs::RenderMode renderMode) const {
    if (_clusterBuffer) {
        batch.setUniformBuffer(ShapePipeline::Slot::BUFFER::SKINNING, _clusterBuffer);
    }
    batch.setModelTransform(_transform);
}

void ModelMeshPartPayload::render(RenderArgs* args) {
    PerformanceTimer perfTimer("ModelMeshPartPayload::render");

    if (!args) {
        return;
    }

    gpu::Batch& batch = *(args->_batch);

    bindTransform(batch, args->_renderMode);

    //Bind the index buffer and vertex buffer and Blend shapes if needed
    bindMesh(batch);

    // apply material properties
    RenderPipelines::bindMaterial(_drawMaterials.top().material, batch, args->_enableTexturing);
    args->_details._materialSwitches++;

    // Draw!
    {
        PerformanceTimer perfTimer("batch.drawIndexed()");
        drawCall(batch);
    }

    const int INDICES_PER_TRIANGLE = 3;
    args->_details._trianglesRendered += _drawPart._numIndices / INDICES_PER_TRIANGLE;
}


void ModelMeshPartPayload::computeAdjustedLocalBound(const std::vector<TransformType>& clusterTransforms) {
    _adjustedLocalBound = _localBound;
    if (clusterTransforms.size() > 0) {
#if defined(SKIN_DQ)
        Transform rootTransform(clusterTransforms[0].getRotation(),
                                clusterTransforms[0].getScale(),
                                clusterTransforms[0].getTranslation());
        _adjustedLocalBound.transform(rootTransform);
#else
        _adjustedLocalBound.transform(clusterTransforms[0]);
#endif

        for (int i = 1; i < (int)clusterTransforms.size(); ++i) {
            AABox clusterBound = _localBound;
#if defined(SKIN_DQ)
            Transform transform(clusterTransforms[i].getRotation(),
                                clusterTransforms[i].getScale(),
                                clusterTransforms[i].getTranslation());
            clusterBound.transform(transform);
#else
            clusterBound.transform(clusterTransforms[i]);
#endif
            _adjustedLocalBound += clusterBound;
        }
    }
}
