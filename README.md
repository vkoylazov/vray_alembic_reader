# vray_alembic_reader

An Alembic reader for V-Ray

## General

This plugin is intended to be a more general-purpose .vrmesh and Alembic reader than the VRayProxy and provides ways to modify the geometry in the .vrmesh/Alembic file in various ways, as well as to assign shaders at render time etc.

## Material assignment rules

The XML file with the material assignment rules has the following format:

```
<materialAssignmentRules>
  <patternRule>
    <pattern>/pTorus/*</pattern>
    <material>orangeMtl</material>
    <displacement amount="5.0">displTex1</displacement>
  </patternRule>
  <patternRule>
    <pattern>*</pattern>
    <material>checkerMtl</material>
    <subdivision>1</subdivision>
  </patternRule>
  ...
</materialAssignmentsRules>
```
