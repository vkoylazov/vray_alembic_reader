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

#include "globalnewdelete.cpp"

using namespace VR;

const int useSingleMapChannelParam=true;

//*************************************************************

struct MyVectorListParam: VRayPluginParameter {
	const tchar* getName(void) { return "vertices"; }
	int getCount(double time) { return p.size(); }
	
	VectorList getVectorList(double time) {
		if (fabs(time-frameTime)<1e-6f) return p;

		int numVerts=p.size();
		VectorList res(numVerts);
		for (int i=0; i<numVerts; i++) {
			res[i]=p[i]+v[i]*(time-frameTime);
		}
		return res;
	}

	VRayParameterType getType(int index, double time) { return paramtype_vector; }

	VectorList& getVerts(void) { return p; }
	VectorList& getVelocities(void) { return v; }
	void setFrameTime(double time) { frameTime=time; }
private:
	double frameTime;
	VectorList p; // vertices
	VectorList v; // velocities
};

// Holds information about a GeomStaticMesh plugin created for each object from the Alembic file.
struct AlembicMeshSource {
	VRayPlugin *geomStaticMesh; // The GeomStaticMesh plugin along with its parameters.

	DefVectorListParam verticesParam;
	DefIntListParam facesParam;

	AlembicMeshSource(void):verticesParam("vertices"), facesParam("faces"), geomStaticMesh(NULL) {
	}
};

struct AlembicMeshInstance {
	AlembicMeshSource *meshSource;
	TraceTransform tm;

	VRayStaticGeometry *meshInstance; // The instance returned from the GeomStaticMesh object
	CharString userAttr; // User attributes
	int meshIndex; // The index of the instance

	AlembicMeshInstance(void):meshSource(NULL), meshInstance(NULL), meshIndex(-1) {
	}
};

struct GeomAlembicReader: VRayStaticGeomSource, VRaySceneModifierInterface {
	GeomAlembicReader(VRayPluginDesc *desc): VRayStaticGeomSource(desc) {
		paramList->setParamCache("file", &fileName, true /* resolvePath */);
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
	AlembicMeshSource *createGeomStaticMesh(MeshVoxel &voxel, int createInstance);

	// Create a material to use for shading
	VRayPlugin* createMaterial(void);

	friend struct GeomAlembicReaderInstance;
};

struct GeomAlembicReader_Params: VRayParameterListDesc {
	GeomAlembicReader_Params(void) {
		addParamString("file", "", -1, NULL, "displayName=(Mesh File), fileAsset=(vrmesh;abc), fileAssetNames=(V-Ray Mesh;Alembic), fileAssetOp=(load)");
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
		MaterialInterface *mtl=getMaterial(reader->baseMtl);
		BSDFInterface *bsdf=getBSDF(reader->baseMtl);

		createMeshInstances(mtl, bsdf, renderID, NULL, NULL, TraceTransform(1), objectID, userAttrs.ptr(), primaryVisibility);

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

	void createMeshInstances(MaterialInterface *_mtl, BSDFInterface *_bsdf, int renderID, VolumetricInterface *volume, LightList *lightList, const TraceTransform &baseTM, int objectID, const tchar *userAttr, int primaryVisibility) {
		int numInstances=reader->meshInstances.count();
		for (int i=0; i<numInstances; i++) {
			AlembicMeshInstance *abcInstance=reader->meshInstances[i];
			if (!abcInstance)
				continue;

			StaticGeomSourceInterface *geom=static_cast<StaticGeomSourceInterface*>(GET_INTERFACE(abcInstance->meshSource->geomStaticMesh, EXT_STATIC_GEOM_SOURCE));
			if (geom) {
				abcInstance->meshInstance=geom->newInstance(getMaterial(reader->baseMtl), getBSDF(reader->baseMtl), renderID, NULL, lightList, baseTM, objectID, userAttr, primaryVisibility);
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

	MaterialInterface* getMaterial(VRayPlugin *mtl) {
		return static_cast<MaterialInterface*>(GET_INTERFACE(mtl, EXT_MATERIAL));
	}

	BSDFInterface* getBSDF(VRayPlugin *mtl) {
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

AlembicMeshSource* GeomAlembicReader::createGeomStaticMesh(MeshVoxel &voxel, int createInstance) {
	// Create our geometry plugin
	tchar meshPluginName[512]="";
	vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%i", meshSources.count());

	VRayPlugin *meshPlugin=newPlugin("GeomStaticMesh", meshPluginName);
	if (!meshPlugin)
		return NULL;

	const MeshChannel *vertsChannel=voxel.getChannel(VERT_GEOM_CHANNEL);
	const MeshChannel *facesChannel=voxel.getChannel(FACE_TOPO_CHANNEL);

	if (!vertsChannel || !vertsChannel->data || !facesChannel || !facesChannel->data)
		return NULL;

	VertGeomData *verts=static_cast<VertGeomData*>(vertsChannel->data);
	FaceTopoData *faces=static_cast<FaceTopoData*>(facesChannel->data);

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

	if (createInstance) {
		AlembicMeshInstance *abcMeshInstance=new AlembicMeshInstance;

		abcMeshInstance->meshIndex=meshInstances.count();
		abcMeshInstance->meshSource=abcMeshSource;
		abcMeshInstance->tm.makeIdentity();

		meshInstances+=abcMeshInstance;
	}

	return abcMeshSource;
}

void GeomAlembicReader::loadGeometry(int frameNumber, VRayRenderer *vray) {
	const VRaySequenceData &sdata=vray->getSequenceData();

	const tchar *fname=fileName.ptr();
	if (!fname) fname="";

	MeshFile *alembicFile=newDefaultMeshFile(fname);
	if (!alembicFile) {
		if (sdata.progress) {
			sdata.progress->error("Cannot open file \"%s\"", fname);
		}
		return;
	}

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
			AlembicMeshSource *abcMeshSource=createGeomStaticMesh(*voxel, true);
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
	brdfPlugin->setParameter(factory.saveInFactory(new DefColorParam("color", Color(0.8f, 0.5f, 0.3f))));

	VRayPlugin *mtlPlugin=newPlugin("MtlSingleBRDF", "diffuseMtl");
	mtlPlugin->setParameter(factory.saveInFactory(new DefPluginParam("brdf", brdfPlugin)));

	return mtlPlugin;
}
