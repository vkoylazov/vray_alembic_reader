#pragma once

#include "utils.h"
#include "charstring.h"
#include "misc.h"
#include "vrayplugins.h"
#include "pxml.h"

/// A structure that describes a material assignment rule from an object name pattern to a material interface.
struct MtlAssignmentRule {
	VR::CharString objNamePattern; // A pattern for the object names that should have this material.
	VR::CharString mtlName; // The name of the material.
	VR::VRayPlugin *mtlPlugin; // The material itself.

	MtlAssignmentRule(void):mtlPlugin(NULL) {}
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
	/// @retval The material plugin that should be used for that object, or NULL if no material
	/// assignment rule can be found that is applicable to the object.
	VR::VRayPlugin *getMaterialPlugin(const VR::CharString &objName);
protected:
	VR::Table<MtlAssignmentRule, -1> mtlAssignmentRulesTable;
};
