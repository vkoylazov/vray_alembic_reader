#include "mtl_assignment_rules.h"
#include "pxml.h"
#include "vraysceneplugman.h"
#include "parse.h"

using namespace VR;

ErrorCode MtlAssignmentRulesTable::readFromXML(PXML &pxml, VR::VRayScene &vrayScene, const CharString &mtlPrefix, ProgressCallback *prog) {
	mtlAssignmentRulesTable.clear();

	// Create all material assignment rules
	int mtlAssignmentsNodeIdx=pxml.FindFullTag("materialAssignmentRules");
	if (mtlAssignmentsNodeIdx>=0) {
		int mtlAssignmentNode=pxml.FindChild(mtlAssignmentsNodeIdx, "patternRule", -1);
		while (mtlAssignmentNode>=0) {
			int materialNodeIdx=pxml.FindFullSubTag(mtlAssignmentNode, "material");
			if (materialNodeIdx>=0) {
				const NODEI &mtlNode=pxml[materialNodeIdx];
				int patternNodeIdx=pxml.FindChild(mtlAssignmentNode, "pattern", -1);
				while (patternNodeIdx>=0) {
					const NODEI &patternNode=pxml[patternNodeIdx];
					MtlAssignmentRule &rule=*mtlAssignmentRulesTable.newElement();
					rule.objNamePattern=patternNode.getData();
					rule.mtlName=mtlNode.getData();
					patternNodeIdx=pxml.FindChild(mtlAssignmentNode, "pattern", patternNodeIdx);
				}
			}

			mtlAssignmentNode=pxml.FindChild(mtlAssignmentsNodeIdx, "patternRule", mtlAssignmentNode);
		}
	}

	// Go through all the rules and resolve the plugin names from the given V-Ray scene.
	for (int i=0; i<mtlAssignmentRulesTable.count(); i++) {
		MtlAssignmentRule &rule=mtlAssignmentRulesTable[i];

		CharString fullName(mtlPrefix);
		fullName.append(rule.mtlName);

		rule.mtlPlugin=vrayScene.findPlugin(fullName.ptr());
		if (NULL==rule.mtlPlugin && NULL!=prog) {
			prog->warning("Cannot find material \"%s\"", rule.mtlName.ptr());
		}
	}

	return ErrorCode();
}

VRayPlugin* MtlAssignmentRulesTable::getMaterialPlugin(const VR::CharString &objName) {
	if (objName.empty())
		return NULL;

	int ruleIndex=-1;
	for (int i=0; i<mtlAssignmentRulesTable.count(); i++) {
		const MtlAssignmentRule &rule=mtlAssignmentRulesTable[i];
		if (!rule.objNamePattern.empty() && matchWildcard(rule.objNamePattern.ptr(), objName.ptr())) {
			ruleIndex=i;
			break;
		}
	}

	VRayPlugin *res=NULL;
	if (ruleIndex>=0)
		res=mtlAssignmentRulesTable[ruleIndex].mtlPlugin;

	return res;
}
