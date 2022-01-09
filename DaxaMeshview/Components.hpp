#pragma once

#include "Daxa.hpp"

struct Primitive {
	u32 indexCount = 0;
	daxa::gpu::BufferHandle indiexBuffer = {};
	daxa::gpu::BufferHandle vertexPositions = {};
	daxa::gpu::BufferHandle vertexUVs = {};
	daxa::gpu::ImageHandle image = {};
};

struct ModelComp {
	std::vector<Primitive> meshes;
};

struct ChildComp {
	daxa::EntityHandle parent = {};
};