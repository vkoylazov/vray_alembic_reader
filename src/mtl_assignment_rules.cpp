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
		int patternRuleNode=pxml.FindChild(mtlAssignmentsNodeIdx, "patternRule", -1);
		while (patternRuleNode>=0) {
			// Find a material tag for this rule
			int materialNodeIdx=pxml.FindFullSubTag(patternRuleNode, "material");

			// Find the displacement tag for this rule.
			int displacementNodeIdx=pxml.FindFullSubTag(patternRuleNode, "displacement");

			// Find the subdivision tag for this rule.
			int subdivNodeIdx=pxml.FindFullSubTag(patternRuleNode, "subdivision");

			// Enumerate all patterns in the rule and create entries for them in the respective tables.
			int patternNodeIdx=pxml.FindChild(patternRuleNode, "pattern", -1);
			while (patternNodeIdx>=0) {
				const NODEI &patternNode=pxml[patternNodeIdx];

				// If there is a material tag, create a material assignment entry.
				if (materialNodeIdx>=0) {
					const NODEI &mtlNode=pxml[materialNodeIdx];
					MtlAssignmentRule &rule=*mtlAssignmentRulesTable.newElement();
					rule.objNamePattern=patternNode.getData();
					rule.mtlName=mtlNode.getData();
				}

				// If there is a displacement tag, create a displacement assignment entry.
				if (displacementNodeIdx>=0) {
					NODEI &displNode=pxml[displacementNodeIdx];
					DisplacementAssignmentRule &rule=*displacementAssignmentRulesTable.newElement();
					rule.objNamePattern=patternNode.getData();
					rule.displTexName=displNode.getData();

					// Check for displacement amount parameter.
					PStrPairList *displParams=displNode.getPairs();
					if (displParams) {
						for (int i=0; i<displParams->count(); i++) {
							const StrPair &strPair=(*displParams)[i];
							if (strPair.par && 0==stricmp(strPair.par, "amount") && strPair.val) {
								sscanf(strPair.val, "%f", &rule.displAmount);
							}
						}
					}
				}

				// If there is a subdivision tag, create a subdivision assignment entry.
				if (subdivNodeIdx>=0) {
					const NODEI &subdivNode=pxml[subdivNodeIdx];
					SubdivAssignmentRule &rule=*subdivAssignmentRulesTable.newElement();
					rule.objNamePattern=patternNode.getData();

					const tchar *subdivStr=subdivNode.getData();
					if (subdivStr) {
						rule.subdivide=atoi(subdivStr);
					}
				}

				// Find the next pattern in the rule.
				patternNodeIdx=pxml.FindChild(patternRuleNode, "pattern", patternNodeIdx);
			}

			// Find the next pattern rule.
			patternRuleNode=pxml.FindChild(mtlAssignmentsNodeIdx, "patternRule", patternRuleNode);
		}
	}

	// Go through all the material rules and resolve the plugin names from the given V-Ray scene.
	for (int i=0; i<mtlAssignmentRulesTable.count(); i++) {
		MtlAssignmentRule &rule=mtlAssignmentRulesTable[i];

		CharString fullName(mtlPrefix);
		fullName.append(rule.mtlName);

		rule.mtlPlugin=vrayScene.findPlugin(fullName.ptr());
		if (nullptr==rule.mtlPlugin && nullptr!=prog) {
			prog->warning("Cannot find material \"%s\"", rule.mtlName.ptr());
		}
	}

	// Go through all the displacement rules and resolve the plugin names for displacement textures from the given V-Ray scene.
	for (int i=0; i<displacementAssignmentRulesTable.count(); i++) {
		DisplacementAssignmentRule &rule=displacementAssignmentRulesTable[i];

		CharString fullName(mtlPrefix);
		fullName.append(rule.displTexName);

		rule.displTexPlugin=vrayScene.findPlugin(fullName.ptr());
		if (nullptr==rule.displTexPlugin && nullptr!=prog) {
			prog->warning("Cannot find displacement texture \"%s\"", rule.displTexName.ptr());
		}
	}

	return ErrorCode();
}

VRayPlugin* MtlAssignmentRulesTable::getMaterialPlugin(const VR::CharString &objName) {
	if (objName.empty())
		return nullptr;

	int ruleIndex=-1;
	for (int i=0; i<mtlAssignmentRulesTable.count(); i++) {
		const MtlAssignmentRule &rule=mtlAssignmentRulesTable[i];
		if (!rule.objNamePattern.empty() && matchWildcard(rule.objNamePattern.ptr(), objName.ptr())) {
			ruleIndex=i;
			break;
		}
	}

	VRayPlugin *res=nullptr;
	if (ruleIndex>=0)
		res=mtlAssignmentRulesTable[ruleIndex].mtlPlugin;

	return res;
}

VRayPlugin* MtlAssignmentRulesTable::getDisplacementTexturePlugin(const VR::CharString &objName, float &amount) {
	if (objName.empty())
		return nullptr;

	int ruleIndex=-1;
	for (int i=0; i<displacementAssignmentRulesTable.count(); i++) {
		const DisplacementAssignmentRule &rule=displacementAssignmentRulesTable[i];
		if (!rule.objNamePattern.empty() && matchWildcard(rule.objNamePattern.ptr(), objName.ptr())) {
			ruleIndex=i;
			break;
		}
	}

	VRayPlugin *res=nullptr;
	if (ruleIndex>=0) {
		const DisplacementAssignmentRule &rule=displacementAssignmentRulesTable[ruleIndex];
		res=rule.displTexPlugin;
		amount=rule.displAmount;
	}

	return res;
}

int MtlAssignmentRulesTable::getSubdivisionEnabled(const VR::CharString &objName) {
	if (objName.empty())
		return false;

	int ruleIndex=-1;
	for (int i=0; i<subdivAssignmentRulesTable.count(); i++) {
		const SubdivAssignmentRule &rule=subdivAssignmentRulesTable[i];
		if (!rule.objNamePattern.empty() && matchWildcard(rule.objNamePattern.ptr(), objName.ptr())) {
			ruleIndex=i;
			break;
		}
	}

	int res=false;
	if (ruleIndex>=0)
		res=subdivAssignmentRulesTable[ruleIndex].subdivide;

	return res;
}
