#include "geomalembicreader.h"

using namespace VR;

//*************************************************************
// GeomAlembicReaderInstance

struct GeomAlembicReaderInstance: VRayStaticGeometry {
	GeomAlembicReaderInstance(GeomAlembicReader *abcReader):reader(abcReader) {
	}

	void compileGeometry(VR::VRayRenderer *vray, const VR::Transform *_tm, double *_times, int _tmCount) VRAY_OVERRIDE {
		createMeshInstances(renderID, NULL, NULL, Transform(1), objectID, userAttrs.ptr(), primaryVisibility);

		const VRayFrameData &fdata=vray->getFrameData();

		// Scratchpad arrays for computing transformation matrices
		TransformsList transforms;

		// Allocate memory for mesh transformations
		// Transform *tms=(Transform*) alloca(tmCount*sizeof(Transform));

		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			// Compute the transformations for this mesh
			double *times=&(abcInstance->times[0]);
			
			// Apply the transformation of the main alembic reader to the local transformations of the instances.
			// Note that both may have a different number of time steps, so the blending is a bit more convoluted.
			multiplyTransforms(transforms, abcInstance->tms, abcInstance->times, _tm, _times, _tmCount);

			vassert(transforms.count()==abcInstance->times.count());

			abcInstance->meshInstance->compileGeometry(vray, &transforms[0], times, transforms.count());
		}
	}

	void clearGeometry(VR::VRayRenderer *vray) VRAY_OVERRIDE {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			abcInstance->meshInstance->clearGeometry(vray);
		}
		deleteMeshInstances();
	}

	void updateMaterial(MaterialInterface *mtl, BSDFInterface *bsdf, int renderID, VolumetricInterface *volume, LightList *lightList, int objectID) VRAY_OVERRIDE {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance || !abcInstance->meshInstance)
				continue;

			abcInstance->meshInstance->updateMaterial(mtl, bsdf, renderID, volume, lightList, objectID);
		}
	}

	VRayShadeData* getShadeData(const VRayContext &rc) VRAY_OVERRIDE { return NULL; }
	VRayShadeInstance* getShadeInstance(const VRayContext &rc) VRAY_OVERRIDE { return NULL; }

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

	static MaterialInterface* getMaterial(VRayPlugin *mtl) {
		return static_cast<MaterialInterface*>(GET_INTERFACE(mtl, EXT_MATERIAL));
	}

	static BSDFInterface* getBSDF(VRayPlugin *mtl) {
		return static_cast<BSDFInterface*>(GET_INTERFACE(mtl, EXT_BSDF));
	}

	void createMeshInstances(int renderID, VolumetricInterface *volume, LightList *lightList, const Transform &baseTM, int objectID, const tchar *userAttr, int primaryVisibility) {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance)
				continue;

			VRayPlugin *geomPlugin=abcInstance->meshSource->getGeomPlugin();

			StaticGeomSourceInterface *geom=static_cast<StaticGeomSourceInterface*>(GET_INTERFACE(geomPlugin, EXT_STATIC_GEOM_SOURCE));
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

			VRayPlugin *geomPlugin=abcInstance->meshSource->getGeomPlugin();

			StaticGeomSourceInterface *geom=static_cast<StaticGeomSourceInterface*>(GET_INTERFACE(geomPlugin, EXT_STATIC_GEOM_SOURCE));
			geom->deleteInstance(abcInstance->meshInstance);
			abcInstance->meshInstance=NULL;
		}
	}

	/// Multiply an array of local transformations with an array of global transforms.
	void multiplyTransforms(
		TransformsList &result, ///< The result is stored here and has the same number of elements as localTransforms.
		const TransformsList &localTransforms, ///< The list of local transforms.
		const TimesList &localTimes, ///< The times when the local transforms were sampled, in increasing order.
		const Transform *tms, ///< An array of global transforms.
		double *times, ///< The times when the global transforms were sampled.
		int tmCount ///< The number of global transforms.
	) const {
		int numLocalTMs=localTransforms.count();
		result.setCount(numLocalTMs);
		for (int i=0; i<numLocalTMs; i++) {
			double localTime=localTimes[i];
			result[i]=getInterpolatedTransform(tms, times, tmCount, localTime)*localTransforms[i];
		}
	}

	/// Intepolate a transform based on a list of keyframes and times.
	/// @param tms The list of transform keyframes.
	/// @param times The times when each keyframe was sampled.
	/// @param tmCount The number of keyframes.
	/// @param time The time at which we want to compute an interpolated transform.
	Transform getInterpolatedTransform(const Transform *tms, double *times, int tmCount, double time) const {
		if (tmCount==1)
			return tms[0];

		if (time<=times[0])
			return tms[0];

		if (time>=times[tmCount-1])
			return tms[tmCount-1];

		// Find the index of the transform for which (times[idx]<=time && time<times[idx+1])
		int idx=0;
		while (idx+1<tmCount && times[idx+1]<time) idx++;

		// If we didn't find a proper time value, just return the last keyframe
		if (idx+1==tmCount)
			return tms[tmCount-1];

		// Interpolate the transforms on either size of time. We use linear interpolation for simplicity.
		float k=float((time-times[idx])/(times[idx+1]-times[idx]));
		Transform res=tms[idx]*(1.0f-k)+tms[idx+1]*k;
		return res;
	}
};

//*************************************************************
// GeomAlembicReader

VRayStaticGeometry* GeomAlembicReader::newInstance(
	MaterialInterface *_mtl,
	BSDFInterface *_bsdf,
	int renderID,
	VolumetricInterface *volume,
	LightList *lightList,
	const Transform &baseTM,
	int objectID,
	const tchar *userAttr,
	int primaryVisibility
) {
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
	const VRayFrameData &fdata=vray->getFrameData();

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

	int numTimeSamples=geomSamples;
	if (!sdata.params.moblur.on) numTimeSamples=1; // No motion blur
	else if (numTimeSamples==0) numTimeSamples=sdata.params.moblur.geomSamples; // Default samples.

	// Motion blur params
	AlembicParams abcParams;
	abcParams.mbOn=(numTimeSamples>1)? sdata.params.moblur.on : false;
	abcParams.mbTimeIndices=numTimeSamples;
	abcParams.mbDuration=sdata.params.moblur.duration;
	abcParams.mbIntervalCenter=sdata.params.moblur.intervalCenter;

	alembicFile->setAdditionalParams(&abcParams);

	ErrorCode res=alembicFile->init(fname);
	if (res.error()) {
		if (sdata.progress) {
			CharString errStr=res.getErrorString();
			sdata.progress->error("Cannot initialize file \"%s\": %s", fname, errStr.ptr());
		}
	} else {
		float time=float(frameNumber);
		alembicFile->setCurrentFrame(time);

		int numVoxels=alembicFile->getNumVoxels();

		// First find out the preview voxel and read the information about
		// UV and color sets from it.
		DefaultMeshSetsData setsData;
		for (int i=0; i<numVoxels; i++) {
			uint32 flags=alembicFile->getVoxelFlags(i);
			if (flags & MVF_PREVIEW_VOXEL) {
				MeshVoxel *previewVoxel=alembicFile->getVoxel(i, numTimeSamples<<16, NULL, NULL);
				if (previewVoxel) {
					VUtils::MeshChannel *mayaInfoChannel=previewVoxel->getChannel(MAYA_INFO_CHANNEL);
					if (mayaInfoChannel) {
						setsData.readFromBuffer((uint8*) mayaInfoChannel->data, mayaInfoChannel->elementSize*mayaInfoChannel->numElements);
					}
					alembicFile->releaseVoxel(previewVoxel);
				}
				break;
			}
		}

		// Go through all the voxels and create the corresponding geometry.
		for (int i=0; i<numVoxels; i++) {
			// Determine if this voxel contains a mesh
			uint32 flags=alembicFile->getVoxelFlags(i);
			if (flags & MVF_PREVIEW_VOXEL) // We don't care about the preview voxel
				continue;
			if (0==(flags & MVF_GEOMETRY_VOXEL)) // Not a mesh voxel; will deal with hair/particles later on
				continue;
			if (0!=(flags & MVF_INSTANCE_VOXEL)) // We are only interested in the source meshes here, we deal with instances separately
				continue;

			// Create a GeomStaticMesh plugin for this voxel
			AlembicMeshSource *abcMeshSource=createGeomStaticMesh(
				vray,
				*alembicFile,
				i,
				true,
				setsData,
				numTimeSamples,
				fdata.frameStart,
				fdata.frameEnd,
				fdata.t
			);
			if (abcMeshSource) {
				meshSources+=abcMeshSource;
			}
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

		if (abcMeshSource->displSubdivPlugin) {
			deletePlugin(abcMeshSource->displSubdivPlugin);
			abcMeshSource->displSubdivPlugin=nullptr;
		}

		if (abcMeshSource->geomStaticMesh) {
			deletePlugin(abcMeshSource->geomStaticMesh);
			abcMeshSource->geomStaticMesh=nullptr;
		}
		delete abcMeshSource;
	}

	meshSources.clear();
}

VRayPlugin* GeomAlembicReader::createDefaultMaterial(void) {
	Transform uvwTransform(
		Matrix(
			Vector(5.0f, 0.0f, 0.0f),
			Vector(0.0f, 5.0f, 0.0f),
			Vector(0.0f, 0.0f, 5.0f)
		),
		Vector(0.0f, 0.0f, 0.0f)
	);

	VRayPlugin *uvwgenPlugin=newPlugin("UVWGenChannel", "uvwgen");
	int res=uvwgenPlugin->setParameter(factory.saveInFactory(new DefTransformParam("uvw_transform", uvwTransform)));
	res=uvwgenPlugin->setParameter(factory.saveInFactory(new DefIntParam("uvw_channel", 0)));

	VRayPlugin *checkerPlugin=newPlugin("TexChecker", "checker");
	res=checkerPlugin->setParameter(factory.saveInFactory(new DefPluginParam("uvwgen", uvwgenPlugin)));
	res=checkerPlugin->setParameter(factory.saveInFactory(new DefColorParam("white_color", Color(0.8f, 0.5f, 0.2f))));
	res=checkerPlugin->setParameter(factory.saveInFactory(new DefColorParam("black_color", Color(0.2f, 0.5f, 0.8f))));

	VRayPlugin *brdfPlugin=newPlugin("BRDFDiffuse", "diffuse");
	res=brdfPlugin->setParameter(factory.saveInFactory(new DefPluginParam("color_tex", checkerPlugin)));

	VRayPlugin *mtlPlugin=newPlugin("MtlSingleBRDF", "diffuseMtl");
	res=mtlPlugin->setParameter(factory.saveInFactory(new DefPluginParam("brdf", brdfPlugin)));

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
	"Sun",
	"MayaLight"
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

	// Append the material definition .vrscene file to the current scene; filter out
	// any plugins that we are not interested in (render settings, cameras, geometry etc).
	FilterCallback filterCallback;
	res=vrayScene.readFileEx(fname.ptr(), &filterCallback, prefix.ptr(), true /* create plugins */, prog);

	if (!res.error())
		mtlPrefix=prefix; // If the file was read successfully, use the prefix.
	else
		mtlPrefix.clear(); // Otherwise, no prefix - will look into the current scene only.

	return res;
}

ErrorCode GeomAlembicReader::readMtlAssignmentsFile(const CharString &fname, PXML &pxml) {
	ErrorCode res=pxml.ParseFileStrict(fname.ptr());
	if (res.error())
		return ErrorCode(res, __FUNCTION__, -1, "Failed to parse XML file");
	return ErrorCode();
}

// Return the material plugin to use for the given Alembic file name
VRayPlugin* GeomAlembicReader::getMaterialPluginForInstance(const CharString &abcName) {
	VRayPlugin *res=mtlAssignments.getMaterialPlugin(abcName);
	if (!res)
		res=defaultMtl;

	return res;
}

void GeomAlembicReader::getDisplacementSubdivParams(const VR::CharString &abcName, DisplacementSubdivParams &params) {
	params.displacementTex=mtlAssignments.getDisplacementTexturePlugin(abcName, params.displacementAmount);
	params.hasSubdivision=mtlAssignments.getSubdivisionEnabled(abcName);
}

