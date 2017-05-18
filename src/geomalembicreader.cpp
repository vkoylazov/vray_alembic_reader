#include "geomalembicreader.h"

using namespace VR;

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
			sdata.progress->warning("Failed to read XML material assignments file \"%s\": %s", mtlAssignmentsFileName.ptr(), errStr.ptr());
		} else {
			// Parse the material assignments from the control file.
			mtlAssignments.readFromXML(pxml, *vrayScene, mtlsPrefix, sdata.progress);
		}
	}

	// Create a default material.
	defaultMtl=createDefaultMaterial();
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

VRayPlugin * GeomAlembicReader::createDefaultMaterial(void) {
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

ErrorCode GeomAlembicReader::readMtlAssignmentsFile(const CharString &fname, PXML &pxml) {
	int err=pxml.ParseFile(fname.ptr());
	if (err)
		return ErrorCode(__FUNCTION__, err, "Failed to parse XML file");
	return ErrorCode();
}

// Return the material plugin to use for the given Alembic file name
VRayPlugin* GeomAlembicReader::getMaterialPluginForInstance(const CharString &abcName) {
	VRayPlugin *res=mtlAssignments.getMaterialPlugin(abcName);
	if (!res)
		res=defaultMtl;

	return res;
}
