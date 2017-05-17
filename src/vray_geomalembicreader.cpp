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

#include "globalnewdelete.cpp"

using namespace VR;

const int useSingleMapChannelParam=true;

//*************************************************************

// Holds information about a GeomStaticMesh plugin created for each object from the Alembic file.
struct AlembicMeshSource {
	VRayPlugin *geomStaticMesh; // The GeomStaticMesh plugin along with its parameters.

	DefVectorListParam verticesParam;
	DefIntListParam facesParam;

	DefVectorListParam normalsParam;
	DefIntListParam faceNormalsParam;

	DefVectorListParam velocitiesParam;

	AlembicMeshSource(void):
		verticesParam("vertices"),
		facesParam("faces"),
		normalsParam("normals"),
		faceNormalsParam("faceNormals"),
		velocitiesParam("velocities"),
		geomStaticMesh(NULL)
	{}
};

struct AlembicMeshInstance {
	AlembicMeshSource *meshSource;
	TraceTransform tm;
	CharString abcName; // The full Alembic name that corresponds to this instance

	VRayStaticGeometry *meshInstance; // The instance returned from the GeomStaticMesh object
	CharString userAttr; // User attributes
	int meshIndex; // The index of the instance

	AlembicMeshInstance(void):meshSource(NULL), meshInstance(NULL), meshIndex(-1) {
	}
};

//********************************************************
// GeomAlembicReader
struct GeomAlembicReader: VRayStaticGeomSource, VRaySceneModifierInterface {
	GeomAlembicReader(VRayPluginDesc *desc): VRayStaticGeomSource(desc) {
		paramList->setParamCache("file", &fileName, true /* resolvePath */);
		paramList->setParamCache("mtl_defs_file", &mtlDefsFileName, true /* resolvePath */);
		paramList->setParamCache("mtl_assignments_file", &mtlAssignmentsFileName, true /* resolvePath */);

		plugman=NULL;
	}
	~GeomAlembicReader(void) {
		plugman=NULL;
	}

	PluginInterface* newInterface(InterfaceID id) VRAY_OVERRIDE {
		return (id==EXT_SCENE_MODIFIER)? static_cast<VRaySceneModifierInterface*>(this) : VRayStaticGeomSource::newInterface(id);
	}

	PluginBase* getPlugin(void) VRAY_OVERRIDE { return this; }

	// From GeomSourceExtension
	VRayStaticGeometry* newInstance(MaterialInterface *mtl, BSDFInterface *bsdf, int renderID, VolumetricInterface *volume, LightList *lightList, const TraceTransform &baseTM, int objectID, const tchar *userAttr, int primaryVisibility) VRAY_OVERRIDE;
	void deleteInstance(VRayStaticGeometry *instance) VRAY_OVERRIDE;

	// From VRayPlugin
	void frameBegin(VR::VRayRenderer *vray) VRAY_OVERRIDE;
	void frameEnd(VR::VRayRenderer *vray) VRAY_OVERRIDE;

	// From VRaySceneModifierInterface
	void preRenderBegin(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where the new plugins will be created
	void postRenderEnd(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where we destroy our plugins

private:
	// Cached parameters
	CharString fileName;
	CharString mtlDefsFileName;
	CharString mtlAssignmentsFileName;

	// The material plugin
	VRayPlugin *baseMtl;

	Table<AlembicMeshSource*, -1> meshSources;
	Table<AlembicMeshInstance*, -1> meshInstances;

	void freeMem(void);

	// Shading stuff
	PluginManager *plugman; // The plugin manager we'll be using to create run-time shaders
	Factory factory; // A class to hold the parameters of the run-time plugins

	// A helper method to create a new plugin in the plugin manager and add it to the plugins set
	// so that we can delete it later.
	VRayPlugin *newPlugin(const tchar *pluginType, const tchar *pluginName);

	// A helper method to delete a plugin from the plugin manager and to remove it from the plugins set.
	void deletePlugin(VRayPlugin *plugin);

	// This generates the actual geometry (vertices, faces etc) at the start of each frame
	void loadGeometry(int frameNumber, VRayRenderer *vray);

	// This unloads the geometry created for each frame and deletes the created GeomStaticMeshPlugins.
	void unloadGeometry(VRayRenderer *vray);

	// The scene offset, cached in frameBegin()
	TracePoint sceneOffset;

	typedef HashSet<VRayPlugin*> PluginsSet;
	PluginsSet plugins; // A list of created plugins; used to delete them at the render end

	// Create a new AlembicMeshSource from the given MeshVoxel.
	AlembicMeshSource *createGeomStaticMesh(VRayRenderer *vray, MeshFile &abcFile, MeshVoxel &voxel, int createInstance);

	// Create a material to use for shading
	VRayPlugin* createMaterial(void);

	friend struct GeomAlembicReaderInstance;

	CharString mtlsPrefix; // The prefix to use when specifying plugins from the materials definitions file.

	/// Read material definitions from the specified file and merge them into the given scene.
	/// The plugin names are prefixed with the file name in case several readers reference the same
	/// material definitions file.
	/// @param[in] fileName The .vrscene file name with the material definitions.
	/// @param prog A progress callback to print information from parsing the .vrscene file.
	/// @param[out] mtlPrefix The prefix that was prepended to all plugins when reading the .vrscene file.
	/// @param[out] vrayScene The scene to create the material plugins into.
	static ErrorCode readMaterialDefinitions(const CharString &fileName, ProgressCallback *prog, CharString &mtlPrefix, VRayScene &vrayScene);

	// Parse the given XML control file into the controlFileXML member.
	static ErrorCode readMtlAssignmentsFile(const CharString &xmlFile, PXML &mtlAssignmentsFileXML);

	// The material assignment rules extracted from controlFileXML
	MtlAssignmentRulesTable mtlAssignments;

	// Return the material plugin to use for the given Alembic file name
	VRayPlugin* getMaterialPluginForInstance(const CharString &abcName);
};

struct GeomAlembicReader_Params: VRayParameterListDesc {
	GeomAlembicReader_Params(void) {
		addParamString("file", "", -1, "The source Alembic or .vrmesh file", "displayName=(Mesh File), fileAsset=(vrmesh;abc), fileAssetNames=(V-Ray Mesh;Alembic), fileAssetOp=(load)");
		addParamString("mtl_defs_file", "", -1, "An optional .vrscene file with material definitions. If not specified, look for the materials in the current scene", "fileAsset=(vrscene), fileAssetNames=(V-Ray Scene), fileAssetOp=(load)");
		addParamString("mtl_assignments_file", "", -1, "An optional XML file that controls material assignments, visibility, displacement, subdivision etc", "fileAsset=(xml), fileAssetNames=(XML control file), fileAssetOp=(load)");
	}
};

#define GeomAlembicReader_PluginID PluginID(LARGE_CONST(2017051657))
static VRAY3_CONST_COMPAT tchar *libText = "Alembic reader for V-Ray";
static const tchar *descText = "A more advanced .vrmesh/Alembic reader for V-Ray";
#ifdef __VRAY40__
SIMPLE_PLUGIN_LIBRARY(GeomAlembicReader_PluginID, "GeomAlembicReader", libText, descText, GeomAlembicReader, GeomAlembicReader_Params, EXT_STATIC_GEOM_SOURCE);
#else
SIMPLE_PLUGIN_LIBRARY(GeomAlembicReader_PluginID, EXT_STATIC_GEOM_SOURCE, "GeomAlembicReader", libText, GeomAlembicReader, GeomAlembicReader_Params);
#endif

//*************************************************************
// GeomAlembicReaderInstance

struct GeomAlembicReaderInstance: VRayStaticGeometry {
	GeomAlembicReaderInstance(GeomAlembicReader *abcReader):reader(abcReader) {
	}

	void compileGeometry(VR::VRayRenderer *vray, VR::TraceTransform *tm, double *times, int tmCount) {
		createMeshInstances(renderID, NULL, NULL, TraceTransform(1), objectID, userAttrs.ptr(), primaryVisibility);

		double tmTime=vray->getFrameData().t;

		// Allocate memory for mesh transformations
		TraceTransform *tms=(TraceTransform*) alloca(tmCount*sizeof(TraceTransform));

		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			// Compute the transformations for this mesh
			for (int k=0; k<tmCount; k++) {
				tms[k]=tm[k]*(abcInstance->tm);
			}

			abcInstance->meshInstance->compileGeometry(vray, tms, times, tmCount);
		}
	}

	void clearGeometry(VR::VRayRenderer *vray) {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			abcInstance->meshInstance->clearGeometry(vray);
		}
		deleteMeshInstances();
	}

	void updateMaterial(MaterialInterface *mtl, BSDFInterface *bsdf, int renderID, VolumetricInterface *volume, LightList *lightList, int objectID) {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			abcInstance->meshInstance->updateMaterial(mtl, bsdf, renderID, volume, lightList, objectID);
		}
	}

	void createMeshInstances(int renderID, VolumetricInterface *volume, LightList *lightList, const TraceTransform &baseTM, int objectID, const tchar *userAttr, int primaryVisibility) {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance)
				continue;

			StaticGeomSourceInterface *geom=static_cast<StaticGeomSourceInterface*>(GET_INTERFACE(abcInstance->meshSource->geomStaticMesh, EXT_STATIC_GEOM_SOURCE));
			if (geom) {
				VRayPlugin *mtlPlugin=reader->getMaterialPluginForInstance(abcInstance->abcName);
				abcInstance->meshInstance=geom->newInstance(getMaterial(mtlPlugin), getBSDF(mtlPlugin), renderID, NULL, lightList, baseTM, objectID, userAttr, primaryVisibility);
			}
		}
	}

	void deleteMeshInstances(void) {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			StaticGeomSourceInterface *geom=static_cast<StaticGeomSourceInterface*>(GET_INTERFACE(abcInstance->meshSource->geomStaticMesh, EXT_STATIC_GEOM_SOURCE));
			geom->deleteInstance(abcInstance->meshInstance);
			abcInstance->meshInstance=NULL;
		}
	}

	static MaterialInterface* getMaterial(VRayPlugin *mtl) {
		return static_cast<MaterialInterface*>(GET_INTERFACE(mtl, EXT_MATERIAL));
	}

	static BSDFInterface* getBSDF(VRayPlugin *mtl) {
		return static_cast<BSDFInterface*>(GET_INTERFACE(mtl, EXT_BSDF));
	}

	VRayShadeData* getShadeData(const VRayContext &rc) { return NULL; }
	VRayShadeInstance* getShadeInstance(const VRayContext &rc) { return NULL; }

	void setPrimaryVisibility(int onOff) { primaryVisibility=onOff; }
	void setRenderID(int id) { renderID=id; }
	void setObjectID(int id) { objectID=id; }
	void setUserAttrs(const tchar *str) { userAttrs=str; }
protected:
	GeomAlembicReader *reader;
	int primaryVisibility;
	int renderID;
	int objectID;
	CharString userAttrs;
};

//*************************************************************
// GeomAlembicReader

VRayStaticGeometry* GeomAlembicReader::newInstance(MaterialInterface *_mtl, BSDFInterface *_bsdf, int renderID, VolumetricInterface *volume, LightList *lightList, const TraceTransform &baseTM, int objectID, const tchar *userAttr, int primaryVisibility) {
	GeomAlembicReaderInstance *abcReaderInstance=new GeomAlembicReaderInstance(this);
	abcReaderInstance->setRenderID(renderID);
	abcReaderInstance->setObjectID(objectID);
	abcReaderInstance->setPrimaryVisibility(primaryVisibility);
	abcReaderInstance->setUserAttrs(userAttr);
	return abcReaderInstance;
}

void GeomAlembicReader::deleteInstance(VRayStaticGeometry *instance) {
	if (!instance)
		return;

	GeomAlembicReaderInstance *abcReaderInstance=static_cast<GeomAlembicReaderInstance*>(instance);
	delete abcReaderInstance;
}

void GeomAlembicReader::preRenderBegin(VR::VRayRenderer *vray) {
	VRayPluginRendererInterface *pluginRenderer=(VRayPluginRendererInterface*) GET_INTERFACE(vray, EXT_PLUGIN_RENDERER);
	if (!pluginRenderer) return; // Don't know how to modify the scene
	plugman=pluginRenderer->getPluginManager();

	// Get access to the current V-Ray scene
	VRayRendererSceneAccess *vraySceneAccess=static_cast<VRayRendererSceneAccess*>(GET_INTERFACE(vray, EXT_VRAYRENDERER_SCENEACCESS));
	if (!vraySceneAccess) return; // We need a V-Ray scene for now.

	VRayScene *vrayScene=vraySceneAccess->getScene();
	if (!vrayScene) return;

	const VRaySequenceData &sdata=vray->getSequenceData();

	// Read the parameters explicitly as there is no-one to do it for us here.
	paramList->cacheParams();

	// Load the materials .vrscene file, if there is one specified
	mtlsPrefix.clear();
	if (!mtlDefsFileName.empty()) {
		ErrorCode err=readMaterialDefinitions(mtlDefsFileName, sdata.progress, mtlsPrefix, *vrayScene);
		if (err.error()) {
			CharString errStr=err.getErrorString();
			sdata.progress->warning("Failed to read material definitions file \"%s\": %s", mtlDefsFileName.ptr(), errStr.ptr());
		}
	}

	if (!mtlAssignmentsFileName.empty()) {
		PXML pxml;
		ErrorCode err=readMtlAssignmentsFile(mtlAssignmentsFileName, pxml);
		if (err.error()) {
			CharString errStr=err.getErrorString();
			sdata.progress->warning("Failed to read XML control file \"%s\": %s", mtlAssignmentsFileName.ptr(), errStr.ptr());
		} else {
			// Parse the material assignments from the control file.
			mtlAssignments.readFromXML(pxml, *vrayScene, mtlsPrefix, sdata.progress);
		}
	}

	// Create a default material.
	baseMtl=createMaterial();
}

void GeomAlembicReader::postRenderEnd(VR::VRayRenderer *vray) {
	if (!plugman) return;

	// Delete all the plugins that we created in preRenderBegin().
	for (PluginsSet::iterator it=plugins.begin(); it!=plugins.end(); it++) {
		VRayPlugin *plugin=it.key();
			plugman->deletePlugin(plugin);
	}
	plugins.clear();

	// Clear all the plugin parameters that we created.
	factory.clear();

	plugman=NULL;
}

void GeomAlembicReader::frameBegin(VR::VRayRenderer *vray) {
	VRayStaticGeomSource::frameBegin(vray);
	double time=vray->getFrameData().t;

	loadGeometry(vray->getFrameData().currentFrame, vray);
	sceneOffset=vray->getFrameData().sceneOffset;
}

void GeomAlembicReader::frameEnd(VR::VRayRenderer *vray) {
	VRayStaticGeomSource::frameEnd(vray);
	unloadGeometry(vray);
}

VRayPlugin* GeomAlembicReader::newPlugin(const tchar *pluginType, const tchar *pluginName) {
	VRayPlugin *res=(VRayPlugin*) plugman->newPlugin(pluginType, NULL);
	if (res) {
		if (pluginName) res->setPluginName(pluginName);
		plugins.insert(res);
	}
	return res;
}

void GeomAlembicReader::deletePlugin(VRayPlugin *plugin) {
	if (plugin) {
		plugman->deletePlugin(plugin);
		plugins.erase(plugin);
	}
}

AlembicMeshSource* GeomAlembicReader::createGeomStaticMesh(VRayRenderer *vray, MeshFile &abcFile, MeshVoxel &voxel, int createInstance) {
	// Create our geometry plugin
	tchar meshPluginName[512]="";
	StringID strID=abcFile.getShaderSetStringID(&voxel, 0);
	if (strID.id!=0) {
		strID=vray->getStringManager()->getStringID(strID.id);
		vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%s", strID.str.ptr());
	} else {
		vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%i", meshSources.count());
	}

	VRayPlugin *meshPlugin=newPlugin("GeomStaticMesh", meshPluginName);
	if (!meshPlugin)
		return NULL;

	const MeshChannel *vertsChannel=voxel.getChannel(VERT_GEOM_CHANNEL);
	const MeshChannel *facesChannel=voxel.getChannel(FACE_TOPO_CHANNEL);

	if (!vertsChannel || !vertsChannel->data || !facesChannel || !facesChannel->data)
		return NULL;

	const VertGeomData *verts=static_cast<VertGeomData*>(vertsChannel->data);
	const FaceTopoData *faces=static_cast<FaceTopoData*>(facesChannel->data);

	AlembicMeshSource *abcMeshSource=new AlembicMeshSource;
	abcMeshSource->geomStaticMesh=meshPlugin;

	meshPlugin->setParameter(&abcMeshSource->verticesParam);
	meshPlugin->setParameter(&abcMeshSource->facesParam);

	// Read the vertices and set them into the verticesParam
	int numVerts=vertsChannel->numElements;
	abcMeshSource->verticesParam.setCount(numVerts);
	for (int i=0; i<numVerts; i++) {
		abcMeshSource->verticesParam[i]=verts[i];
	}

	// Read the faces and set them into the facesParam
	int numFaces=facesChannel->numElements;
	abcMeshSource->facesParam.setCount(numFaces*3);
	for (int i=0; i<numFaces; i++) {
		const FaceTopoData &face=faces[i];

		int idx=i*3;
		abcMeshSource->facesParam[idx+0]=face.v[0];
		abcMeshSource->facesParam[idx+1]=face.v[1];
		abcMeshSource->facesParam[idx+2]=face.v[2];
	}

	// Read the normals and set them into the normalsParam and faceNormalsParam
	const MeshChannel *normalsChannel=voxel.getChannel(VERT_NORMAL_CHANNEL);
	const MeshChannel *faceNormalsChannel=voxel.getChannel(VERT_NORMAL_TOPO_CHANNEL);
	if (normalsChannel && faceNormalsChannel) {
		const VertGeomData *normals=static_cast<VertGeomData*>(normalsChannel->data);
		int numNormals=normalsChannel->numElements;
		abcMeshSource->normalsParam.setCount(numNormals);
		for (int i=0; i<numNormals; i++) {
			abcMeshSource->normalsParam[i]=normals[i];
		}

		const FaceTopoData *faceNormals=static_cast<FaceTopoData*>(faceNormalsChannel->data);
		int numFaceNormals=faceNormalsChannel->numElements;
		abcMeshSource->faceNormalsParam.setCount(numFaceNormals*3);
		for (int i=0; i<numFaceNormals; i++) {
			const FaceTopoData &face=faceNormals[i];

			int idx=i*3;
			abcMeshSource->faceNormalsParam[idx+0]=face.v[0];
			abcMeshSource->faceNormalsParam[idx+1]=face.v[1];
			abcMeshSource->faceNormalsParam[idx+2]=face.v[2];
		}

		meshPlugin->setParameter(&abcMeshSource->normalsParam);
		meshPlugin->setParameter(&abcMeshSource->faceNormalsParam);
	}

	// If motion blur is enabled, read the vertex velocities and set them into the velocitiesParam
	if (vray->getSequenceData().params.moblur.on) {
		const MeshChannel *velocitiesChannel=voxel.getChannel(VERT_VELOCITY_CHANNEL);
		if (velocitiesChannel && velocitiesChannel->data && velocitiesChannel->numElements==numVerts) {
			const VertGeomData *velocities=static_cast<VertGeomData*>(velocitiesChannel->data);
			abcMeshSource->velocitiesParam.setCount(numVerts);
			for (int i=0; i<numVerts; i++) {
				abcMeshSource->velocitiesParam[i]=velocities[i];
			}
			meshPlugin->setParameter(&abcMeshSource->velocitiesParam);		
		}
	}

	if (createInstance) {
		AlembicMeshInstance *abcMeshInstance=new AlembicMeshInstance;

		abcMeshInstance->meshIndex=meshInstances.count();
		abcMeshInstance->meshSource=abcMeshSource;
		abcMeshInstance->tm.makeIdentity();
		abcMeshInstance->abcName=strID.str;

		meshInstances+=abcMeshInstance;
	}

	return abcMeshSource;
}

void GeomAlembicReader::loadGeometry(int frameNumber, VRayRenderer *vray) {
	VRaySequenceData &sdata=vray->getSequenceDataNoConst();

	const tchar *fname=fileName.ptr();
	if (!fname) fname="";

	// Create a reader suitable for the given file name (vrmesh or Alembic)
	MeshFile *alembicFile=newDefaultMeshFile(fname);
	if (!alembicFile) {
		if (sdata.progress) {
			sdata.progress->error("Cannot open file \"%s\"", fname);
		}
		return;
	}

	// Set some parameters for the Alembic reader before we read the file
	alembicFile->setStringManager(vray->getStringManager());
	alembicFile->setThreadManager(vray->getSequenceData().threadManager);
	alembicFile->setUseFullNames(true); // We want to get the full names from the Alembic file

	float fps=24.0f;
	SequenceDataUnitsInfo *unitsInfo=static_cast<SequenceDataUnitsInfo*>(GET_INTERFACE(&sdata, EXT_SDATA_UNITSINFO));
	if (unitsInfo) fps=unitsInfo->framesScale;
	alembicFile->setFramesPerSecond(fps);

	// Motion blur params
	AlembicParams abcParams;
	abcParams.mbOn=sdata.params.moblur.on;
	abcParams.mbTimeIndices=sdata.params.moblur.geomSamples-1;
	abcParams.mbDuration=sdata.params.moblur.duration;
	abcParams.mbIntervalCenter=sdata.params.moblur.intervalCenter;

	alembicFile->setAdditionalParams(&abcParams);

	if (!alembicFile->init(fname)) {
		if (sdata.progress) {
			sdata.progress->error("Cannot initialize file \"%s\"", fname);
		}
	} else {
		alembicFile->setCurrentFrame(float(frameNumber));

		int numVoxels=alembicFile->getNumVoxels();
		for (int i=0; i<numVoxels; i++) {
			// Determine if this voxel contains a mesh
			uint32 flags=alembicFile->getVoxelFlags(i);
			if (flags & MVF_PREVIEW_VOXEL) // We don't care about the preview voxel
				continue;
			if (0==(flags & MVF_GEOMETRY_VOXEL)) // Not a mesh voxel; will deal with hair/particles later on
				continue;
			if (0!=(flags & MVF_INSTANCE_VOXEL)) // We are only interested in the source meshes here, we deal with instances separately
				continue;

			MeshVoxel *voxel=alembicFile->getVoxel(i);
			if (!voxel)
				continue;

			// Create a GeomStaticMesh plugin for this voxel
			AlembicMeshSource *abcMeshSource=createGeomStaticMesh(vray, *alembicFile, *voxel, true);
			if (abcMeshSource) {
				meshSources+=abcMeshSource;
			}

			alembicFile->releaseVoxel(voxel);
		}
	}

	deleteDefaultMeshFile(alembicFile);
}

void GeomAlembicReader::unloadGeometry(VRayRenderer *vray) {
	int numMeshInstances=meshInstances.count();
	for (int i=0; i<numMeshInstances; i++) {
		AlembicMeshInstance *abcMeshInstance=meshInstances[i];
		delete abcMeshInstance;
	}
	meshInstances.clear();

	int numMeshSources=meshSources.count();
	for (int i=0; i<numMeshSources; i++) {
		AlembicMeshSource *abcMeshSource=meshSources[i];
		if (!abcMeshSource)
			continue;

		deletePlugin(abcMeshSource->geomStaticMesh);
		abcMeshSource->geomStaticMesh=NULL;
		delete abcMeshSource;
	}

	meshSources.clear();
}

VRayPlugin * GeomAlembicReader::createMaterial(void) {
	VRayPlugin *brdfPlugin=newPlugin("BRDFDiffuse", "diffuse");
	brdfPlugin->setParameter(factory.saveInFactory(new DefColorParam("color", Color(1.0f, 0.0f, 0.0f))));

	VRayPlugin *mtlPlugin=newPlugin("MtlSingleBRDF", "diffuseMtl");
	mtlPlugin->setParameter(factory.saveInFactory(new DefPluginParam("brdf", brdfPlugin)));

	return mtlPlugin;
}

//***********************************************************

// A list of prefixes for plugin types to ignore when reading
// material definition .vrscene files. This is because such files
// may contain other plugins like rendering settings, geometry,
// camera, lights etc and we want to ignore those and only create
// materials and textures.
const tchar *ignoredPlugins[]={
	"Settings",
	"Geom",
	"RenderView"
	"Camera",
	"Node",
	"Light",
	"Sun"
};

struct FilterCallback: ScenePluginFilter {
	// If the given plugin type starts with any of the prefixes listed in the ingoredPlugins[] array,
	// skip it.
	int filter(const CharString &type, CharString &name, Object *object) VRAY_OVERRIDE {
		for (int i=0; i<COUNT_OF(ignoredPlugins); i++) {
			if (strncmp(type.ptr(), ignoredPlugins[i], strlen(ignoredPlugins[i]))==0) {
				return false;
			}
		}
		return true;
	}
};

ErrorCode GeomAlembicReader::readMaterialDefinitions(const CharString &fname, ProgressCallback *prog, CharString &mtlPrefix, VRayScene &vrayScene) {
	ErrorCode res;

	// For the moment, prefix all plugins in the scene with the name of the
	// material definitions file. In this way, materials with the same name
	// coming out of different material definition files will not mess up with
	// each other.
	CharString prefix=fname;
	prefix.append("_");

	FilterCallback filterCallback;
	res=vrayScene.readFileEx(fname.ptr(), &filterCallback, prefix.ptr(), true /* create plugins */, prog);

	if (!res.error())
		mtlPrefix=prefix; // If the file was read successfully, use the prefix.
	else
		mtlPrefix.clear(); // Otherwise, no prefix - will look into the current scene only.

	return res;
}

ErrorCode GeomAlembicReader::readMtlAssignmentsFile(const CharString &fname, PXML &controlFileXML) {
	ErrorCode res=controlFileXML.ParseFileStrict(fname.ptr());
	return res;
}

// Return the material plugin to use for the given Alembic file name
VRayPlugin* GeomAlembicReader::getMaterialPluginForInstance(const CharString &abcName) {
	VRayPlugin *res=mtlAssignments.getMaterialPlugin(abcName);
	if (!res)
		res=baseMtl;

	return res;
}
