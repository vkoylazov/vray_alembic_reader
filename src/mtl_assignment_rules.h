#pragma once

#include "utils.h"
#include "charstring.h"
#include "misc.h"
#include "vrayplugins.h"
#include "pxml.h"

/// A structure that describes a material assignment rule from an object name pattern to a material plugin.
struct MtlAssignmentRule {
	VR::CharString objNamePattern; ///< A pattern for the object names that should have this material. May contain wildcards * and ?
	VR::CharString mtlName; ///< The name of the material.
	VR::VRayPlugin *mtlPlugin; ///< The material plugin itself (may be NULL).

	MtlAssignmentRule(void):mtlPlugin(nullptr) {}
};

/// A structure that describes displacement assignment rule from an object name to a displacement texture plugin.
struct DisplacementAssignmentRule {
	VR::CharString objNamePattern; ///< A pattern for the object names that should have this displacement texture. May contain wildcards * and ?
	VR::CharString displTexName; ///< The name of the displacement texture.
	VR::VRayPlugin *displTexPlugin; ///< The displacement texture plugin itself (may be NULL).
	float displAmount;

	DisplacementAssignmentRule(void):displTexPlugin(nullptr), displAmount(1.0f) {}
};

/// A structure that describes subdivision surface rule from an object name.
struct SubdivAssignmentRule {
	VR::CharString objNamePattern; ///< A pattern for the object names that should be subdivided.
	int subdivide; ///< true if the objects should be subdivided and false otherwise.

	SubdivAssignmentRule(void):subdivide(true) {}
};

/// A table of material assignment rules.
struct MtlAssignmentRulesTable {
	/// Read the material assignment rules from the given XML file.
	/// @param pxml The parsed XML file.
	/// @param scene The V-Ray scene with the material plugins.
	/// @param mtlsPrefix A prefix that is added to the material plugins.
	VR::ErrorCode readFromXML(PXML &pxml, VR::VRayScene &scene, const VR::CharString &mtlsPrefix, VR::ProgressCallback *prog);

	/// Find and return the material plugin that corresponds to a given object name.
	/// @param objName The object name (coming from the Alembic file).
	/// @retval The material plugin that should be used for that object, or nullptr if no material
	/// assignment rule can be found that is applicable to the object.
	VR::VRayPlugin* getMaterialPlugin(const VR::CharString &objName);

	/// Find and return the displacement texture plugin that corresponds to a given object name.
	/// @param objName The object name (coming from the Alembic file).
	/// @retval The displacement texture plugin that should be used for that object or nullptr if
	/// the object has no displacement assignment rule for it.
	VR::VRayPlugin* getDisplacementTexturePlugin(const VR::CharString &objName, float &displAmount);

	/// Return true if the specified object should have view-dependent subdivision enabled.
	int getSubdivisionEnabled(const VR::CharString &objName);
protected:
	VR::Table<MtlAssignmentRule, -1> mtlAssignmentRulesTable;
	VR::Table<DisplacementAssignmentRule, -1> displacementAssignmentRulesTable;
	VR::Table<SubdivAssignmentRule, -1> subdivAssignmentRulesTable;
};
