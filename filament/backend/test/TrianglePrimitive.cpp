/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "TrianglePrimitive.h"

#include <filament/MaterialEnums.h>

namespace test {

using namespace filament;
using namespace filament::backend;

static constexpr filament::math::float2 gVertices[3] = {
    { -1.0, -1.0 },
    {  1.0, -1.0 },
    { -1.0,  1.0 }
};

static constexpr short gIndices[3] = { 0, 1, 2 };

TrianglePrimitive::TrianglePrimitive(filament::backend::DriverApi& driverApi,
        bool allocateLargeBuffers) : mDriverApi(driverApi) {
    mVertexCount = allocateLargeBuffers ? 2048 : 3;
    mIndexCount = allocateLargeBuffers ? 4096 : 3;
    AttributeArray attributes = {
            Attribute {
                    .offset = 0,
                    .stride = sizeof(filament::math::float2),
                    .buffer = 0,
                    .type = ElementType::FLOAT2,
                    .flags = 0
            }
    };

   // Backends do not (and should not) know the semantics of each vertex attribute, but they
   // need to know whether the vertex shader consumes them as integers or as floats.
   // NOTE: This flag needs to be set regardless of whether the attribute is actually declared.
   attributes[BONE_INDICES].flags |= Attribute::FLAG_INTEGER_TARGET;

    AttributeBitset enabledAttributes;
    enabledAttributes.set(VertexAttribute::POSITION);

    const size_t size = sizeof(math::float2) * 3;
    mBufferObject = mDriverApi.createBufferObject(size, BufferObjectBinding::VERTEX, BufferUsage::STATIC);
    mVertexBuffer = mDriverApi.createVertexBuffer(1, 1, mVertexCount, attributes,
            BufferUsage::STATIC);
    mDriverApi.setVertexBufferObject(mVertexBuffer, 0, mBufferObject);
    BufferDescriptor vertexBufferDesc(gVertices, size, nullptr);
    mDriverApi.updateBufferObject(mBufferObject, std::move(vertexBufferDesc), 0);

    mIndexBuffer = mDriverApi.createIndexBuffer(ElementType::SHORT, mIndexCount,
            BufferUsage::STATIC);
    BufferDescriptor indexBufferDesc(gIndices, sizeof(short) * 3, nullptr);
    mDriverApi.updateIndexBuffer(mIndexBuffer, std::move(indexBufferDesc), 0);

    mRenderPrimitive = mDriverApi.createRenderPrimitive(0);

    mDriverApi.setRenderPrimitiveBuffer(mRenderPrimitive, mVertexBuffer, mIndexBuffer);
    mDriverApi.setRenderPrimitiveRange(mRenderPrimitive, PrimitiveType::TRIANGLES, 0, 0, 2, 3);
}

void TrianglePrimitive::updateVertices(const filament::math::float2 vertices[3]) noexcept {
    void* buffer = malloc(sizeof(filament::math::float2) * mVertexCount);
    filament::math::float2* vertBuffer = (filament::math::float2*) buffer;
    std::copy(vertices, vertices + 3, vertBuffer);

    BufferDescriptor vBuffer(vertBuffer, sizeof(filament::math::float2) * mVertexCount,
            [] (void* buffer, size_t size, void* user) {
        free(buffer);
    });
    mDriverApi.updateBufferObject(mBufferObject, std::move(vBuffer), 0);
}

void TrianglePrimitive::updateIndices(const short indices[3]) noexcept {
    void* buffer = malloc(sizeof(short) * mIndexCount);
    short* indexBuffer = (short*) buffer;
    std::copy(indices, indices + 3, indexBuffer);

    BufferDescriptor bufferDesc(indexBuffer, sizeof(short) * mIndexCount,
            [] (void* buffer, size_t size, void* user) {
        free(buffer);
    });
    mDriverApi.updateIndexBuffer(mIndexBuffer, std::move(bufferDesc), 0);
}

void TrianglePrimitive::updateIndices(const short* indices, int count, int offset) noexcept {
    void* buffer = malloc(sizeof(short) * count);
    short* indexBuffer = (short*) buffer;
    std::copy(indices, indices + count, indexBuffer);

    BufferDescriptor bufferDesc(indexBuffer, sizeof(short) * count,
            [] (void* buffer, size_t size, void* user) {
        free(buffer);
    });
    mDriverApi.updateIndexBuffer(mIndexBuffer, std::move(bufferDesc), offset * sizeof(short));
}

TrianglePrimitive::~TrianglePrimitive() {
    mDriverApi.destroyBufferObject(mBufferObject);
    mDriverApi.destroyVertexBuffer(mVertexBuffer);
    mDriverApi.destroyIndexBuffer(mIndexBuffer);
    mDriverApi.destroyRenderPrimitive(mRenderPrimitive);
}

TrianglePrimitive::PrimitiveHandle TrianglePrimitive::getRenderPrimitive() const noexcept {
    return mRenderPrimitive;
}

} // namespae test
