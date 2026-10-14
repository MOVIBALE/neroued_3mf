// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "neroued/3mf/builder.h"

#include "neroued/3mf/error.h"

#include "internal/validate.h"
#include "opc.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace neroued_3mf {

void DocumentBuilder::CheckNotBuilt() const {
    if (built_) { throw InputError("Builder already consumed by Build()"); }
}

Object &DocumentBuilder::FindObjectById(uint32_t id) {
    auto it = object_id_index_.find(id);
    if (it == object_id_index_.end()) {
        throw InputError("No object with id " + std::to_string(id));
    }
    return objects_[it->second];
}

uint32_t DocumentBuilder::NextId() {
    return next_id_++;
}

DocumentBuilder &DocumentBuilder::SetUnit(Unit unit) {
    CheckNotBuilt();
    unit_ = unit;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetLanguage(std::string lang) {
    CheckNotBuilt();
    language_ = std::move(lang);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddMetadata(std::string name, std::string value,
                                              std::string type) {
    CheckNotBuilt();
    metadata_.push_back({std::move(name), std::move(value), std::move(type)});
    return *this;
}

DocumentBuilder &DocumentBuilder::AddNamespace(std::string prefix, std::string uri) {
    CheckNotBuilt();
    for (auto &ns : custom_namespaces_) {
        if (ns.prefix == prefix) {
            ns.uri = std::move(uri);
            return *this;
        }
    }
    custom_namespaces_.push_back({std::move(prefix), std::move(uri)});
    return *this;
}

uint32_t DocumentBuilder::AddBaseMaterialGroup(std::vector<BaseMaterial> materials) {
    CheckNotBuilt();
    if (materials.empty()) { throw InputError("BaseMaterialGroup must not be empty"); }
    uint32_t id = NextId();
    base_material_groups_.push_back({id, std::move(materials)});
    return id;
}

uint32_t DocumentBuilder::AddMeshObject(std::string name, MeshView mesh,
                                        std::optional<uint32_t> pid,
                                        std::optional<uint32_t> pindex) {
    CheckNotBuilt();
    uint32_t id = NextId();
    Object obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.mesh = mesh;
    obj.pid = pid;
    obj.pindex = pindex;
    objects_.push_back(std::move(obj));
    object_id_index_.emplace(id, objects_.size() - 1);
    return id;
}

uint32_t DocumentBuilder::AddMeshObject(std::string name, Mesh &&mesh, std::optional<uint32_t> pid,
                                        std::optional<uint32_t> pindex) {
    CheckNotBuilt();
    owned_meshes_.push_back(std::move(mesh));
    MeshView view = owned_meshes_.back().View();
    uint32_t id = NextId();
    Object obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.mesh = view;
    obj.pid = pid;
    obj.pindex = pindex;
    objects_.push_back(std::move(obj));
    object_id_index_.emplace(id, objects_.size() - 1);
    return id;
}

uint32_t DocumentBuilder::AddComponentObject(std::string name, std::vector<Component> components) {
    CheckNotBuilt();
    if (components.empty()) {
        throw InputError("Component object must have at least one component");
    }
    uint32_t id = NextId();
    Object obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.components = std::move(components);
    objects_.push_back(std::move(obj));
    object_id_index_.emplace(id, objects_.size() - 1);
    return id;
}

DocumentBuilder &DocumentBuilder::SetObjectType(uint32_t object_id, ObjectType type) {
    CheckNotBuilt();
    FindObjectById(object_id).type = type;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetPartNumber(uint32_t object_id, std::string partnumber) {
    CheckNotBuilt();
    FindObjectById(object_id).partnumber = std::move(partnumber);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddObjectMetadata(uint32_t object_id, std::string name,
                                                    std::string value, std::string type) {
    CheckNotBuilt();
    FindObjectById(object_id).metadata.push_back(
        {std::move(name), std::move(value), std::move(type)});
    return *this;
}

DocumentBuilder &DocumentBuilder::SetComponentTransform(uint32_t object_id, Transform transform) {
    CheckNotBuilt();
    FindObjectById(object_id).component_transform = transform;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetObjectUUID(uint32_t object_id, std::string uuid) {
    CheckNotBuilt();
    FindObjectById(object_id).uuid = std::move(uuid);
    return *this;
}

DocumentBuilder &DocumentBuilder::SetComponentUUID(uint32_t object_id, std::string uuid) {
    CheckNotBuilt();
    FindObjectById(object_id).component_uuid = std::move(uuid);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddBuildItem(uint32_t object_id, Transform transform,
                                               std::string partnumber, std::string uuid) {
    return AddBuildItem(object_id, transform, std::move(partnumber), std::move(uuid), {});
}

DocumentBuilder &DocumentBuilder::AddBuildItem(uint32_t object_id, Transform transform,
                                               std::string partnumber, std::string uuid,
                                               std::map<std::string, std::string> attributes) {
    CheckNotBuilt();
    build_items_.push_back(
        {object_id, transform, std::move(partnumber), std::move(uuid), std::move(attributes)});
    return *this;
}

DocumentBuilder &DocumentBuilder::SetThumbnail(std::vector<uint8_t> data,
                                               std::string content_type) {
    CheckNotBuilt();
    thumbnail_ = Thumbnail{std::move(data), std::move(content_type)};
    return *this;
}

DocumentBuilder &DocumentBuilder::AddCustomPart(CustomPart part) {
    CheckNotBuilt();
    custom_parts_.push_back(std::move(part));
    return *this;
}

DocumentBuilder &DocumentBuilder::AddCustomRelationship(CustomRelationship rel) {
    CheckNotBuilt();
    custom_relationships_.push_back(std::move(rel));
    return *this;
}

DocumentBuilder &DocumentBuilder::AddCustomContentType(CustomContentType ct) {
    CheckNotBuilt();
    custom_content_types_.push_back(std::move(ct));
    return *this;
}

DocumentBuilder &DocumentBuilder::EnableProduction(Transform assembly_transform) {
    CheckNotBuilt();
    production_.enabled = true;
    production_.assembly_build_transform = assembly_transform;
    bool has_p = std::any_of(custom_namespaces_.begin(), custom_namespaces_.end(),
                             [](const XmlNamespace &ns) { return ns.prefix == "p"; });
    if (!has_p) {
        custom_namespaces_.push_back(
            {"p", "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"});
    }
    return *this;
}

DocumentBuilder &DocumentBuilder::SetProductionMergeObjects(bool merge) {
    CheckNotBuilt();
    production_.merge_objects = merge;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetProductionBuildUUID(std::string uuid) {
    CheckNotBuilt();
    production_.build_uuid = std::move(uuid);
    return *this;
}

DocumentBuilder &DocumentBuilder::SetProductionAssemblyUUID(std::string uuid) {
    CheckNotBuilt();
    production_.assembly_uuid = std::move(uuid);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddExternalModelMetadata(std::string name, std::string value,
                                                           std::string type) {
    CheckNotBuilt();
    production_.external_model_metadata.push_back(
        {std::move(name), std::move(value), std::move(type)});
    return *this;
}

uint32_t DocumentBuilder::AddProductionGroup(std::string name, std::vector<uint32_t> object_ids,
                                             std::string uuid) {
    CheckNotBuilt();
    if (!production_.enabled) { throw InputError("Production groups require Production mode"); }
    if (name.empty()) { throw InputError("Production group name is empty"); }
    if (object_ids.empty()) { throw InputError("Production group has no objects"); }

    std::unordered_set<uint32_t> unique_ids;
    for (const auto object_id : object_ids) {
        if (!unique_ids.insert(object_id).second) {
            throw InputError("Production group repeats object id " + std::to_string(object_id));
        }
        const auto &object = FindObjectById(object_id);
        if (object.mesh.vertices.empty() || !object.components.empty()) {
            throw InputError("Production groups require mesh objects; invalid id " +
                             std::to_string(object_id));
        }
        if (grouped_object_ids_.contains(object_id)) {
            throw InputError("Production group repeats previously assigned object id " +
                             std::to_string(object_id));
        }
    }

    const uint32_t assembly_id = NextId();
    production_.groups.push_back(
        {assembly_id, std::move(name), std::move(object_ids), std::move(uuid)});
    grouped_object_ids_.insert(unique_ids.begin(), unique_ids.end());
    return assembly_id;
}

ProductionLayout DocumentBuilder::PlanProductionLayout() const {
    CheckNotBuilt();
    if (!production_.enabled) { throw InputError("Production layout requires production mode"); }
    if (build_items_.empty()) {
        throw InputError("Production layout requires at least one build item");
    }
    std::unordered_set<uint32_t> resource_ids;
    for (const auto &object : objects_) { resource_ids.insert(object.id); }
    for (const auto &group : base_material_groups_) { resource_ids.insert(group.id); }
    detail::ValidateProductionGroups(objects_, production_, build_items_, resource_ids);
    if (production_.groups.empty()) {
        for (const auto &item : build_items_) {
            if (!object_id_index_.contains(item.object_id)) {
                throw InputError("BuildItem references non-existent object id " +
                                 std::to_string(item.object_id));
            }
        }
    }
    return detail::BuildProductionLayout(objects_, production_);
}

Document DocumentBuilder::Build() {
    CheckNotBuilt();
    built_ = true;

    Document doc;
    doc.unit = unit_;
    doc.language = std::move(language_);
    doc.metadata = std::move(metadata_);
    doc.custom_namespaces = std::move(custom_namespaces_);
    doc.base_material_groups = std::move(base_material_groups_);
    doc.objects = std::move(objects_);
    doc.build_items = std::move(build_items_);
    doc.thumbnail = std::move(thumbnail_);
    doc.custom_parts = std::move(custom_parts_);
    doc.custom_relationships = std::move(custom_relationships_);
    doc.custom_content_types = std::move(custom_content_types_);
    doc.production = std::move(production_);
    // std::vector move preserves element addresses, so existing
    // MeshView spans referencing owned_meshes_ remain valid.
    doc.owned_meshes = std::move(owned_meshes_);

    detail::ValidateDocument(doc);
    return doc;
}

} // namespace neroued_3mf
