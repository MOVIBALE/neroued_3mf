// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file document.h
/// \brief 3MF Document model with non-owning views and optional owned meshes.

#include "materials.h"
#include "types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neroued_3mf {

enum class Unit { Micron, Millimeter, Centimeter, Inch, Foot, Meter };

enum class ObjectType { Model, SolidSupport, Support, Surface, Other };

struct Metadata {
    std::string name;
    std::string value;
    std::string type; ///< Optional type annotation (e.g. "xs:string"); empty = omit.
};

struct XmlNamespace {
    std::string prefix;
    std::string uri;
};

/// Intra-model component reference (3MF Core Spec 4.1.1).
struct Component {
    uint32_t object_id = 0;
    Transform transform = Transform::Identity();
};

struct Object {
    uint32_t id = 0;
    std::string name;
    ObjectType type = ObjectType::Model;
    std::string partnumber;

    MeshView mesh;
    /// Core-spec components (within same model file). Mutually exclusive with mesh.
    std::vector<Component> components;

    std::optional<uint32_t> pid;
    std::optional<uint32_t> pindex;

    std::vector<Metadata> metadata;

    /// Transform applied when this object is referenced as a Production Extension component.
    Transform component_transform = Transform::Identity();

    /// Production Extension p:UUID; empty = omit.
    std::string uuid;
    /// Production Extension p:UUID for this object's root component reference.
    std::string component_uuid;
};

struct BuildItem {
    uint32_t object_id = 0;
    Transform transform = Transform::Identity();
    std::string partnumber;
    /// Production Extension p:UUID; empty = omit.
    std::string uuid;
    /// Additional XML attributes supplied by the caller; values are XML-escaped.
    /// Standard item attributes must use their dedicated fields above.
    std::map<std::string, std::string> attributes;
};

struct Thumbnail {
    std::vector<uint8_t> data;
    std::string content_type; ///< "image/png" or "image/jpeg"
};

// -- Generic extension injection points for vendor-specific content --

struct CustomPart {
    std::string path_in_zip;
    std::string content_type;
    std::vector<uint8_t> data;
};

struct CustomRelationship {
    std::string source_part; ///< e.g. "3D/3dmodel.model"; empty = package root
    std::string id;
    std::string type;
    std::string target;
};

struct CustomContentType {
    std::string extension;
    std::string content_type;
};

/// Read-only Production layout calculated from the same object/path rules as
/// the OPC writer.  Paths use the leading slash required by p:path.
struct ProductionObjectLayout {
    uint32_t object_id = 0;
    std::string path;
};

/// Explicit group of mesh objects written together to one external model.
struct ProductionGroup {
    uint32_t assembly_id = 0;
    std::string name;
    std::vector<uint32_t> object_ids;
    /// Production Extension p:UUID for the generated assembly object.
    std::string uuid;
};

/// Read-only layout for one explicit external Production model group.
struct ProductionGroupLayout {
    uint32_t assembly_id = 0;
    std::string name;
    std::string path;
    std::vector<ProductionObjectLayout> objects;
};

struct ProductionLayout {
    uint32_t assembly_id = 0;
    std::string main_model_path = "3D/3dmodel.model";
    std::vector<ProductionObjectLayout> external_objects;
    std::vector<ProductionGroupLayout> groups;
};

struct Document {
    /// Move-only because Object::mesh may reference data in owned_meshes.
    Document() = default;
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;
    Document(Document &&) noexcept = default;
    Document &operator=(Document &&) noexcept = default;

    Unit unit = Unit::Millimeter;
    std::string language = "en-US";
    std::vector<Metadata> metadata;
    std::vector<XmlNamespace> custom_namespaces;

    std::vector<BaseMaterialGroup> base_material_groups;
    std::vector<Object> objects;
    std::vector<BuildItem> build_items;

    std::optional<Thumbnail> thumbnail;

    std::vector<CustomPart> custom_parts;
    std::vector<CustomRelationship> custom_relationships;
    std::vector<CustomContentType> custom_content_types;

    struct ProductionConfig {
        bool enabled = false;
        Transform assembly_build_transform = Transform::Identity();
        bool merge_objects = false;
        std::vector<Metadata> external_model_metadata;
        /// Production Extension UUID for the root build element.
        std::string build_uuid;
        /// Production Extension UUID for the generated assembly object.
        std::string assembly_uuid;
        /// When non-empty, each group becomes one root assembly/build item and
        /// one external model file. Objects must be assigned exactly once.
        std::vector<ProductionGroup> groups;
    } production;

    /// Mesh data owned by this Document (populated when DocumentBuilder
    /// receives Mesh&& via AddMeshObject). Objects' MeshView spans reference
    /// elements in this vector. Empty when all meshes are external (MeshView mode).
    std::vector<Mesh> owned_meshes;
};

} // namespace neroued_3mf
