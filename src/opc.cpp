// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "opc.h"

#include "neroued/3mf/error.h"

#include "internal/validate.h"
#include "internal/xml_util.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace neroued_3mf::detail {

namespace {

constexpr std::string_view kModelRelType =
    "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel";
constexpr std::string_view kModelContentType =
    "application/vnd.ms-package.3dmanufacturing-3dmodel+xml";
constexpr std::string_view kRelationshipContentType =
    "application/vnd.openxmlformats-package.relationships+xml";
constexpr std::string_view kProductionNs =
    "http://schemas.microsoft.com/3dmanufacturing/production/2015/06";
constexpr std::string_view kThumbnailRelType =
    "http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail";

std::string NormalizeZipPath(const std::string &path) {
    if (path.empty()) { throw InputError("OPC part path is empty"); }
    std::string n = path;
    std::replace(n.begin(), n.end(), '\\', '/');
    while (!n.empty() && n.front() == '/') { n.erase(0, 1); }
    if (n.empty()) { throw InputError("OPC part path is empty after normalization"); }
    return n;
}

std::string NormalizePartName(const std::string &part_name) {
    return "/" + NormalizeZipPath(part_name);
}

std::string NormalizeExtension(std::string ext) {
    while (!ext.empty() && ext.front() == '.') { ext.erase(0, 1); }
    if (ext.empty()) { throw InputError("Content type extension is empty"); }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string NormalizeRelSource(const std::string &source) {
    if (source.empty() || source == "/") { return {}; }
    return NormalizeZipPath(source);
}

std::string RelPartPathForSource(const std::string &source) {
    if (source.empty()) { return "_rels/.rels"; }
    std::string s = NormalizeZipPath(source);
    auto slash = s.rfind('/');
    if (slash == std::string::npos) { return "_rels/" + s + ".rels"; }
    return s.substr(0, slash) + "/_rels/" + s.substr(slash + 1) + ".rels";
}

template <typename Map>
void InsertOrValidate(Map &entries, const std::string &key, const std::string &ct,
                      const std::string &kind) {
    if (key.empty()) { throw InputError(kind + " key is empty"); }
    if (ct.empty()) { throw InputError(kind + " content type is empty"); }
    auto [it, ok] = entries.emplace(key, ct);
    if (!ok && it->second != ct) { throw InputError("Conflicting OPC " + kind + " for " + key); }
}

std::string BuildRelationshipsXml(const std::vector<OpcRelationship> &rels) {
    std::string xml;
    xml.reserve(256 + rels.size() * 128);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<Relationships "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    for (const auto &rel : rels) {
        xml += "  <Relationship Target=\"";
        xml += EscapeXml(rel.target);
        xml += "\" Id=\"";
        xml += EscapeXml(rel.id);
        xml += "\" Type=\"";
        xml += EscapeXml(rel.type);
        xml += "\"/>\n";
    }
    xml += "</Relationships>\n";
    return xml;
}

std::string BuildContentTypesXml(const std::map<std::string, std::string> &defaults,
                                 const std::map<std::string, std::string> &overrides) {
    std::string xml;
    xml.reserve(256 + (defaults.size() + overrides.size()) * 128);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<Types "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
    for (const auto &[ext, ct] : defaults) {
        xml += "  <Default Extension=\"";
        xml += EscapeXml(ext);
        xml += "\" ContentType=\"";
        xml += EscapeXml(ct);
        xml += "\"/>\n";
    }
    for (const auto &[pn, ct] : overrides) {
        xml += "  <Override PartName=\"";
        xml += EscapeXml(pn);
        xml += "\" ContentType=\"";
        xml += EscapeXml(ct);
        xml += "\"/>\n";
    }
    xml += "</Types>\n";
    return xml;
}

std::vector<uint8_t> StringToBytes(const std::string &s) {
    return {s.begin(), s.end()};
}

std::string ThumbnailExtension(const std::string &content_type) {
    if (content_type == "image/png") { return "png"; }
    if (content_type == "image/jpeg") { return "jpeg"; }
    return "png";
}

void AppendProductionComponent(std::string &xml, const Object &object,
                               const ProductionObjectLayout &layout) {
    xml += "        <component p:path=\"" + EscapeXml(layout.path) + "\" objectid=\"";
    xml += std::to_string(object.id) + "\"";
    if (!object.component_transform.IsIdentity()) {
        xml += " transform=\"" + SerializeTransform(object.component_transform) + "\"";
    }
    const auto &uuid = object.component_uuid.empty() ? object.uuid : object.component_uuid;
    if (!uuid.empty()) { xml += " p:UUID=\"" + EscapeXml(uuid) + "\""; }
    xml += "/>\n";
}

} // namespace

ProductionLayout BuildProductionLayout(std::span<const Object> objects, bool merge_objects) {
    if (objects.empty()) { throw InputError("3MF document has no mesh objects"); }

    uint32_t assembly_id = 0;
    for (const auto &obj : objects) { assembly_id = std::max(assembly_id, obj.id); }
    ValidateResourceId(assembly_id + 1, "Production assembly id");

    ProductionLayout layout;
    layout.assembly_id = assembly_id + 1;
    layout.external_objects.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        layout.external_objects.push_back({
            objects[i].id,
            merge_objects ? "/3D/Objects/object_1.model"
                          : "/3D/Objects/object_" + std::to_string(i + 1) + ".model",
        });
    }
    return layout;
}

ProductionLayout BuildProductionLayout(std::span<const Object> objects,
                                       const Document::ProductionConfig &production) {
    if (production.groups.empty()) {
        return BuildProductionLayout(objects, production.merge_objects);
    }

    ProductionLayout layout;
    layout.groups.reserve(production.groups.size());
    for (std::size_t group_index = 0; group_index < production.groups.size(); ++group_index) {
        const auto &group = production.groups[group_index];
        ProductionGroupLayout group_layout;
        group_layout.assembly_id = group.assembly_id;
        group_layout.name = group.name;
        group_layout.path = "/3D/Objects/object_" + std::to_string(group_index + 1) + ".model";
        group_layout.objects.reserve(group.object_ids.size());
        for (const auto object_id : group.object_ids) {
            group_layout.objects.push_back({object_id, group_layout.path});
            layout.external_objects.push_back({object_id, group_layout.path});
        }
        layout.groups.push_back(std::move(group_layout));
    }
    return layout;
}

ProductionLayout BuildProductionLayout(const Document &doc) {
    if (doc.objects.empty() || doc.build_items.empty()) {
        throw InputError("3MF document has no mesh/build items");
    }
    return BuildProductionLayout(doc.objects, doc.production);
}

std::vector<OpcPart> BuildOpcParts(const Document &doc) {
    if (doc.objects.empty() || doc.build_items.empty()) {
        throw InputError("3MF document has no mesh/build items");
    }

    std::vector<OpcPart> parts;
    parts.reserve(8 + doc.custom_parts.size());

    const auto production_layout =
        doc.production.enabled ? BuildProductionLayout(doc) : ProductionLayout{};
    const std::string model_path = production_layout.main_model_path;

    // -- relationships by source_part ---------------------------------------
    std::unordered_map<std::string, std::vector<OpcRelationship>> rels_by_source;
    rels_by_source[""] = {
        {.id = "rel0", .type = std::string(kModelRelType), .target = "/" + model_path}};

    // -- thumbnail ----------------------------------------------------------
    std::string thumbnail_path;
    if (doc.thumbnail.has_value() && !doc.thumbnail->data.empty()) {
        std::string ext = ThumbnailExtension(doc.thumbnail->content_type);
        thumbnail_path = "Metadata/thumbnail." + ext;
        parts.push_back({thumbnail_path, doc.thumbnail->content_type, doc.thumbnail->data});
        rels_by_source[""].push_back({.id = "rel-thumb",
                                      .type = std::string(kThumbnailRelType),
                                      .target = "/" + thumbnail_path});
    }

    for (const auto &cr : doc.custom_relationships) {
        std::string src = NormalizeRelSource(cr.source_part);
        rels_by_source[src].push_back({cr.id, cr.type, cr.target});
    }

    // -- collect content types for all known parts --------------------------
    std::unordered_map<std::string, std::string> part_content_types;
    part_content_types.emplace(model_path, std::string(kModelContentType));
    if (!thumbnail_path.empty()) {
        part_content_types.emplace(thumbnail_path, doc.thumbnail->content_type);
    }

    // -- custom parts -------------------------------------------------------
    for (const auto &cp : doc.custom_parts) {
        std::string normalized = NormalizeZipPath(cp.path_in_zip);
        parts.push_back({normalized, cp.content_type, cp.data});
        if (!cp.content_type.empty()) { part_content_types.emplace(normalized, cp.content_type); }
    }

    // -- production extension: assembly model + external object refs ---------
    if (doc.production.enabled) {
        std::unordered_set<std::string> reserved_prefixes;
        reserved_prefixes.insert("p");

        std::string axml;
        axml.reserve(4096);
        axml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"";
        axml += UnitToStringView(doc.unit);
        axml += "\" xml:lang=\"";
        axml += EscapeXml(doc.language);
        axml += "\" xmlns=\"";
        axml += kCoreNs;
        axml += "\" xmlns:p=\"";
        axml += kProductionNs;
        axml += "\"";
        for (const auto &ns : doc.custom_namespaces) {
            if (ns.prefix.empty() || reserved_prefixes.contains(ns.prefix)) { continue; }
            axml += " xmlns:";
            axml += EscapeXml(ns.prefix);
            axml += "=\"";
            axml += EscapeXml(ns.uri);
            axml += "\"";
        }
        axml += " requiredextensions=\"p\">\n";

        for (const auto &md : doc.metadata) {
            if (md.name.empty()) { continue; }
            axml += "  <metadata name=\"";
            axml += EscapeXml(md.name);
            axml += "\"";
            if (!md.type.empty()) {
                axml += " type=\"";
                axml += EscapeXml(md.type);
                axml += "\"";
            }
            axml += ">";
            axml += EscapeXml(md.value);
            axml += "</metadata>\n";
        }

        axml += "  <resources>\n";
        if (doc.production.groups.empty()) {
            const uint32_t assembly_id = production_layout.assembly_id;
            axml += "    <object id=\"";
            axml += std::to_string(assembly_id);
            axml += "\" type=\"model\"";
            if (!doc.production.assembly_uuid.empty()) {
                axml += " p:UUID=\"";
                axml += EscapeXml(doc.production.assembly_uuid);
                axml += "\"";
            }
            axml += ">\n      <components>\n";

            for (std::size_t i = 0; i < doc.objects.size(); ++i) {
                AppendProductionComponent(axml, doc.objects[i],
                                          production_layout.external_objects[i]);
            }
            axml += "      </components>\n    </object>\n  </resources>\n  <build";
            if (!doc.production.build_uuid.empty()) {
                axml += " p:UUID=\"";
                axml += EscapeXml(doc.production.build_uuid);
                axml += "\"";
            }
            axml += ">\n    <item objectid=\"";
            axml += std::to_string(assembly_id);
            axml += "\"";
            if (!doc.production.assembly_build_transform.IsIdentity()) {
                axml += " transform=\"";
                axml += SerializeTransform(doc.production.assembly_build_transform);
                axml += "\"";
            }
            axml += SerializeBuildItemAttributes(doc.build_items.front());
            axml += " printable=\"1\"/>\n  </build>\n</model>\n";
        } else {
            std::unordered_map<uint32_t, const Object *> object_by_id;
            object_by_id.reserve(doc.objects.size());
            for (const auto &object : doc.objects) { object_by_id.emplace(object.id, &object); }
            for (std::size_t group_index = 0; group_index < doc.production.groups.size();
                 ++group_index) {
                const auto &group = doc.production.groups[group_index];
                const auto &group_layout = production_layout.groups[group_index];
                axml += "    <object id=\"";
                axml += std::to_string(group.assembly_id);
                axml += "\" type=\"model\" name=\"";
                axml += EscapeXml(group.name);
                axml += "\"";
                if (!group.uuid.empty()) {
                    axml += " p:UUID=\"";
                    axml += EscapeXml(group.uuid);
                    axml += "\"";
                }
                axml += ">\n      <components>\n";
                for (const auto &object_layout : group_layout.objects) {
                    const auto object = object_by_id.find(object_layout.object_id);
                    if (object == object_by_id.end()) {
                        throw InputError("Production layout references missing object id " +
                                         std::to_string(object_layout.object_id));
                    }
                    AppendProductionComponent(axml, *object->second, object_layout);
                }
                axml += "      </components>\n    </object>\n";
            }
            axml += "  </resources>\n  <build";
            if (!doc.production.build_uuid.empty()) {
                axml += " p:UUID=\"";
                axml += EscapeXml(doc.production.build_uuid);
                axml += "\"";
            }
            axml += ">\n";
            for (const auto &item : doc.build_items) {
                axml += "    <item objectid=\"";
                axml += std::to_string(item.object_id);
                axml += "\"";
                if (!item.transform.IsIdentity()) {
                    axml += " transform=\"";
                    axml += SerializeTransform(item.transform);
                    axml += "\"";
                }
                axml += SerializeBuildItemAttributes(item);
                axml += " printable=\"1\"/>\n";
            }
            axml += "  </build>\n</model>\n";
        }

        parts.push_back({model_path, std::string(kModelContentType), StringToBytes(axml)});

        if (!doc.production.groups.empty()) {
            for (std::size_t i = 0; i < production_layout.groups.size(); ++i) {
                const auto &group = production_layout.groups[i];
                const std::string ext_path = NormalizeZipPath(group.path);
                part_content_types.emplace(ext_path, std::string(kModelContentType));
                rels_by_source[model_path].push_back({.id = "rel-obj-" + std::to_string(i + 1),
                                                      .type = std::string(kModelRelType),
                                                      .target = "/" + ext_path});
            }
        } else if (doc.production.merge_objects) {
            std::string ext_zip = NormalizeZipPath(production_layout.external_objects.front().path);
            part_content_types.emplace(ext_zip, std::string(kModelContentType));
            rels_by_source[model_path].push_back(
                {.id = "rel-obj-1", .type = std::string(kModelRelType), .target = "/" + ext_zip});
        } else {
            for (std::size_t i = 0; i < doc.objects.size(); ++i) {
                std::string ext_path = NormalizeZipPath(production_layout.external_objects[i].path);
                part_content_types.emplace(ext_path, std::string(kModelContentType));
                rels_by_source[model_path].push_back({.id = "rel-obj-" + std::to_string(i + 1),
                                                      .type = std::string(kModelRelType),
                                                      .target = "/" + ext_path});
            }
        }
    }

    // -- relationship parts (sorted for determinism) ------------------------
    std::vector<std::string> source_paths;
    source_paths.reserve(rels_by_source.size());
    for (const auto &[key, _] : rels_by_source) { source_paths.push_back(key); }
    std::sort(source_paths.begin(), source_paths.end());

    for (const auto &src : source_paths) {
        auto rels = std::move(rels_by_source[src]);
        std::sort(rels.begin(), rels.end(),
                  [](const OpcRelationship &a, const OpcRelationship &b) { return a.id < b.id; });
        parts.push_back({RelPartPathForSource(src), std::string(kRelationshipContentType),
                         StringToBytes(BuildRelationshipsXml(rels))});
    }

    // -- [Content_Types].xml ------------------------------------------------
    std::map<std::string, std::string> defaults;
    std::map<std::string, std::string> overrides;
    InsertOrValidate(defaults, "rels", std::string(kRelationshipContentType), "default");
    InsertOrValidate(defaults, "model", std::string(kModelContentType), "default");

    for (const auto &[p, ct] : part_content_types) {
        InsertOrValidate(overrides, NormalizePartName(p), ct, "override");
    }
    for (const auto &cct : doc.custom_content_types) {
        InsertOrValidate(defaults, NormalizeExtension(cct.extension), cct.content_type, "default");
    }

    parts.push_back({"[Content_Types].xml", "application/xml",
                     StringToBytes(BuildContentTypesXml(defaults, overrides))});
    return parts;
}

} // namespace neroued_3mf::detail
