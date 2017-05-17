#pragma once

#include "utils.h"
#include "rayserver.h"
#include "mesh_file.h"
#include "vrayinterface.h"
#include "vrayrenderer.h"
#include "vrayplugins.h"
#include "meshprimitives.h"
#include "geometryclasses.h"
#include "vraytexutils.h"
#include "factory.h"
#include "defparams.h"
#include "mesh_file.h"
#include "vrayoutputoptions.h"
#include "pxml.h"
#include "vraysceneplugman.h"
#include "sceneparser.h"
#include "mtl_assignment_rules.h"

/// Information about a GeomStaticMesh plugin created for each object from the Alembic file.
struct AlembicMeshSource {
	VR::VRayPlugin *geomStaticMesh; ///< The GeomStaticMesh plugin.

	VR::DefVectorListParam verticesParam; ///< The parameter for the vertices.
	VR::DefIntListParam facesParam; ///< The parameter for the faces.

	VR::DefVectorListParam normalsParam; ///< The parameter for the normals.
	VR::DefIntListParam faceNormalsParam; ///< The parameter for the face normals.

	VR::DefVectorListParam velocitiesParam; ///< The parameter for the velocities.

	/// Constructor.
	AlembicMeshSource(void):
		verticesParam("vertices"),
		facesParam("faces"),
		normalsParam("normals"),
		faceNormalsParam("faceNormals"),
		velocitiesParam("velocities"),
		geomStaticMesh(NULL)
	{}
};

/// Information about an instance of an AlembicMeshSource.
struct AlembicMeshInstance {
	AlembicMeshSource *meshSource; ///< The original mesh.
	VR::TraceTransform tm; ///< The transformation matrix.
	VR::CharString abcName; ///< The full Alembic name of this instance from the Alembic file.

	VR::VRayStaticGeometry *meshInstance; ///< The instance returned from the GeomStaticMesh object.
	VR::CharString userAttr; ///< User attributes
	int meshIndex; ///< The index of the instance

	/// Constructor.
	AlembicMeshInstance(void):meshSource(NULL), meshInstance(NULL), meshIndex(-1) {
	}
};

//********************************************************
// GeomAlembicReader

/// The parameter descriptor for the GeomAlembicReader plugin.
struct GeomAlembicReader_Params: VR::VRayParameterListDesc {
	GeomAlembicReader_Params(void) {
		addParamString("file", "", -1, "The source Alembic or .vrmesh file", "displayName=(Mesh File), fileAsset=(vrmesh;abc), fileAssetNames=(V-Ray Mesh;Alembic), fileAssetOp=(load)");
		addParamString("mtl_defs_file", "", -1, "An optional .vrscene file with material definitions. If not specified, look for the materials in the current scene", "fileAsset=(vrscene), fileAssetNames=(V-Ray Scene), fileAssetOp=(load)");
		addParamString("mtl_assignments_file", "", -1, "An optional XML file that controls material assignments, visibility, displacement, subdivision etc", "fileAsset=(xml), fileAssetNames=(XML control file), fileAssetOp=(load)");
	}
};

/// The GeomAlembicReader plugin.
struct GeomAlembicReader: VR::VRayStaticGeomSource, VR::VRaySceneModifierInterface {
	/// Constructor.
	GeomAlembicReader(VR::VRayPluginDesc *desc): VR::VRayStaticGeomSource(desc) {
		paramList->setParamCache("file", &fileName, true /* resolvePath */);
		paramList->setParamCache("mtl_defs_file", &mtlDefsFileName, true /* resolvePath */);
		paramList->setParamCache("mtl_assignments_file", &mtlAssignmentsFileName, true /* resolvePath */);

		plugman=NULL;
	}

	/// Destructor.
	~GeomAlembicReader(void) {
		plugman=NULL;
	}

	/// Return the interfaces that we support.
	PluginInterface* newInterface(InterfaceID id) VRAY_OVERRIDE {
		return (id==EXT_SCENE_MODIFIER)? static_cast<VRaySceneModifierInterface*>(this) : VRayStaticGeomSource::newInterface(id);
	}

	PluginBase* getPlugin(void) VRAY_OVERRIDE { return this; }

	// From GeomSourceExtension
	VR::VRayStaticGeometry* newInstance(VR::MaterialInterface *mtl, VR::BSDFInterface *bsdf, int renderID, VR::VolumetricInterface *volume, VR::LightList *lightList, const VR::TraceTransform &baseTM, int objectID, const tchar *userAttr, int primaryVisibility) VRAY_OVERRIDE;
	void deleteInstance(VR::VRayStaticGeometry *instance) VRAY_OVERRIDE;

	// From VRayPlugin
	void frameBegin(VR::VRayRenderer *vray) VRAY_OVERRIDE;
	void frameEnd(VR::VRayRenderer *vray) VRAY_OVERRIDE;

	// From VRaySceneModifierInterface
	void preRenderBegin(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where the new plugins will be created
	void postRenderEnd(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where we destroy our plugins

private:
	// Cached parameters
	VR::CharString fileName;
	VR::CharString mtlDefsFileName;
	VR::CharString mtlAssignmentsFileName;

	/// A default material for shading objects without material assignment.
	VR::VRayPlugin *defaultMtl;

	/// The mesh plugins that will be instanced for rendering.
	VR::Table<AlembicMeshSource*, -1> meshSources;

	/// The instances that will get rendered.
	VR::Table<AlembicMeshInstance*, -1> meshInstances;

	void freeMem(void);

	PluginManager *plugman; ///< The plugin manager we'll be using to create run-time shaders
	Factory factory; ///< A class to hold the parameters of the run-time plugins

	/// A helper method to create a new plugin in the plugin manager and add it to the plugins set
	/// so that we can delete it later.
	VR::VRayPlugin *newPlugin(const tchar *pluginType, const tchar *pluginName);

	/// A helper method to delete a plugin from the plugin manager and to remove it from the plugins set.
	void deletePlugin(VR::VRayPlugin *plugin);

	/// Generates the actual geometry (vertices, faces etc) at the start of each frame from the Alembic/.vrmesh file.
	void loadGeometry(int frameNumber, VR::VRayRenderer *vray);

	/// Unload the geometry created after each frame and delete the created GeomStaticMesh plugins.
	void unloadGeometry(VR::VRayRenderer *vray);

	typedef VR::HashSet<VR::VRayPlugin*> PluginsSet;
	PluginsSet plugins; ///< A list of created plugins; used to delete them at the render end

	/// Create a new AlembicMeshSource from the given MeshVoxel, along with the associated GeomStaticMesh plugin for it.
	/// @param vray The current V-Ray renderer.
	/// @param abcFile The parsed .vrmesh/Alembic file.
	/// @param voxel The voxel to create a mesh plugin for.
	/// @param createInstance true to also create an AlembicMeshInstance object for the mesh and add it to the meshInstances table.
	/// @retval The resulting AlembicMeshSource object. May be NULL if the object cannot be created.
	AlembicMeshSource *createGeomStaticMesh(VR::VRayRenderer *vray, VR::MeshFile &abcFile, VR::MeshVoxel &voxel, int createInstance);

	/// Create a default material to use for shading when no material assignment is found for an object.
	VRayPlugin* createDefaultMaterial(void);

	friend struct GeomAlembicReaderInstance;

	VR::CharString mtlsPrefix; ///< The prefix to use when specifying plugins from the materials definitions file.

	/// Read material definitions from the specified file and merge them into the given scene.
	/// The plugin names are prefixed with the file name in case several readers reference the same
	/// material definitions file.
	/// @param[in] fileName The .vrscene file name with the material definitions.
	/// @param prog A progress callback to print information from parsing the .vrscene file.
	/// @param[out] mtlPrefix The prefix that was prepended to all plugins when reading the .vrscene file.
	/// @param[out] vrayScene The scene to create the material plugins into.
	static VR::ErrorCode readMaterialDefinitions(const VR::CharString &fileName, VR::ProgressCallback *prog, VR::CharString &mtlPrefix, VR::VRayScene &vrayScene);

	/// Parse the given XML control file into the controlFileXML member.
	static VR::ErrorCode readMtlAssignmentsFile(const VR::CharString &xmlFile, PXML &mtlAssignmentsFileXML);

	/// The material assignment rules extracted from controlFileXML
	MtlAssignmentRulesTable mtlAssignments;

	/// Return the material plugin to use for the given Alembic file name
	VR::VRayPlugin* getMaterialPluginForInstance(const VR::CharString &abcName);
};
