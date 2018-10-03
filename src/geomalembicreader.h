#pragma once

#include "utils.h"
#include "rayserver.h"
#include "mesh_file.h"
#include "mesh_sets_info.h"
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

struct GeomAlembicReader;

typedef VR::Table<VR::CharString> StringList;

/// A single map channel for DefMapChannelsParam.
struct AbcMapChannel {
	int idx; ///< Index of the channel.
	VUtils::Table<VUtils::Vector> verts; ///< Texture vertices.
	VUtils::Table<int> faces; ///< Texture faces.

	void operator=(const AbcMapChannel &mapChan) {
		idx=mapChan.idx;
		verts.copy(mapChan.verts);
		faces.copy(mapChan.faces);
	}
};

/// A list of mapping channels.
typedef VR::Table<AbcMapChannel, -1> AbcMapChannelsList;

template<class T>
inline void copyKeyframeData(T &dst, const T &src) { dst=src; }

inline void copyKeyframeData(AbcMapChannelsList &dst, const AbcMapChannelsList &src) {
	dst.copy(src);
}

inline void copyKeyframeData(StringList &dst, const StringList &src) {
	dst.copy(src);
}

template<class T>
struct AnimatedParam: VR::VRayPluginParameter {
	AnimatedParam(const tchar *name):paramName(name) {}

	const tchar* getName(void) VRAY_OVERRIDE { return paramName; }
	
	void reserveKeyframes(int numKeyframes) {
		keyframes.setCount(numKeyframes, true /* exact */);
		keyframes.setCount(0);
	}

	void addKeyframe(double time, const T &data) {
		if (keyframes.count()>0) {
			vassert(time>keyframes.last().time);
		}

		Keyframe<T> &keyframe=*(keyframes.newElement());
		keyframe.time=time;
		keyframe.data=data;
	}

	T& addKeyframe(double time) {
		if (keyframes.count()>0) {
			vassert(time>keyframes.last().time);
		}

		Keyframe<T> &keyframe=*(keyframes.newElement());
		keyframe.time=time;
		return keyframe.data;
	}

protected:
	const tchar *paramName;

	template<class T>
	struct Keyframe {
		double time;
		T data;

		Keyframe(void):time(0.0f) {}

		Keyframe(const Keyframe &other):time(other.time), data(other.data) {}

		Keyframe& operator=(const Keyframe &other) {
			time=other.time;
			copyKeyframeData(data, other.data);
			return (*this);
		}
	};

	VR::Table<Keyframe<T>, -1> keyframes;

	int getKeyframeIndex(double time) {
		if (keyframes.count()==0)
			return -1;

		if (keyframes.count()==1)
			return 0;

		int res=0;
		for (int i=0; i<keyframes.count(); i++) {
			if (time>keyframes[i].time+1e-12f) {
				res=i;
			} else {
				break;
			}
		}
		return res;
	}

};

template<class T>
struct AnimatedSimpleListParam: AnimatedParam<T> {
	AnimatedSimpleListParam(const tchar *name):AnimatedParam<T>(name) {}

	int getCount(double time) VRAY_OVERRIDE {
		int keyframeIdx=getKeyframeIndex(time);
		if (keyframeIdx==-1)
			return -1;

		return keyframes[keyframeIdx].data.count();
	}
};

struct AnimatedVectorListParam: AnimatedSimpleListParam<VR::VectorList> {
	AnimatedVectorListParam(const tchar *name):AnimatedSimpleListParam<VR::VectorList>(name) {}

	VR::VectorList getVectorList(double time=0.0) VRAY_OVERRIDE {
		int keyframeIdx=getKeyframeIndex(time);
		if (keyframeIdx==-1)
			return VR::VectorList();

		return keyframes[keyframeIdx].data;
	}
	
	VR::VRayParameterType getType(int index, double time=0.0) VRAY_OVERRIDE {
		return VR::paramtype_vector;
	}
};

struct AnimatedIntListParam: AnimatedSimpleListParam<VR::IntList> {
	AnimatedIntListParam(const tchar *name):AnimatedSimpleListParam<VR::IntList>(name) {}

	VR::IntList getIntList(double time=0.0) VRAY_OVERRIDE {
		int keyframeIdx=getKeyframeIndex(time);
		if (keyframeIdx==-1)
			return VR::IntList();

		return keyframes[keyframeIdx].data;
	}
	
	VR::VRayParameterType getType(int index, double time=0.0) VRAY_OVERRIDE {
		return VR::paramtype_int;
	}
};

struct AnimatedStringListParam: AnimatedSimpleListParam<StringList> {
	AnimatedStringListParam(const tchar *name):AnimatedSimpleListParam<StringList>(name) {}

	const tchar* getString(int index, double time) VRAY_OVERRIDE {
		int keyframeIdx=getKeyframeIndex(time);
		if (keyframeIdx==-1)
			return NULL;

		return keyframes[keyframeIdx].data[index].ptr();
	}
	
	VR::VRayParameterType getType(int index, double time=0.0) VRAY_OVERRIDE {
		return VR::paramtype_string;
	}
};

/// Blatant copy of the DefMapChannelsParam; need to improve this in the V-Ray SDK.
struct AnimatedMapChannelsParam: AnimatedParam<AbcMapChannelsList> {
	/// Constructor.
	/// @param paramName The name of the parameter.
	/// @param ownName This flag is equal to 1 if the object owns the name and 0 otherwise.
	AnimatedMapChannelsParam(const tchar *paramName):AnimatedParam<AbcMapChannelsList>(paramName), level(0)
	{}

	int getCount(double time) VRAY_OVERRIDE {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return -1;

		if (level==0) return mapChannels->count();
		else if (level==1) return 3;
		else if (level==2) {
			if (innerIdx==0) return -1;
			else if (innerIdx==1) return (*mapChannels)[chanIdx].verts.count();
			else if (innerIdx==2) return (*mapChannels)[chanIdx].faces.count();
		}
		return mapChannels->count();
	}

	VR::ListHandle openList(int listIdx) VRAY_OVERRIDE {
		level++;
		if (level==1) chanIdx=listIdx;
		else if (level==2) innerIdx=listIdx;
		// vs2015 warns about assigning 32-bit signed int to pointer
		return reinterpret_cast<VR::ListHandle>(static_cast<size_t>(level)); // Any non-NULL value would do.
	}

	void closeList(VR::ListHandle) VRAY_OVERRIDE { level--; }

	int getInt(int index, double time) VRAY_OVERRIDE {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return 0;

		if (level==1 || level==2) return (*mapChannels)[chanIdx].idx;
		else return 0;
	}

	VR::IntList getIntList(double time) VRAY_OVERRIDE {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return VR::IntList();

		if (level==2 && innerIdx==2) return VR::IntList(&((*mapChannels)[chanIdx].faces[0]), (*mapChannels)[chanIdx].faces.count());
		return VR::IntList();
	}

	VR::VectorList getVectorList(double time) VRAY_OVERRIDE {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return VR::VectorList();

		if (level==2 && innerIdx==1) return VR::VectorList(&((*mapChannels)[chanIdx].verts[0]), (*mapChannels)[chanIdx].verts.count());
		return VR::VectorList();
	}

	VR::VRayParameterType getType(int index, double time) VRAY_OVERRIDE {
		if (level==0) return VR::paramtype_list;
		else {
			if (index==-1) {
				if (level==1)
					return VR::paramtype_list;
				else {
					if (innerIdx == 0) return VR::paramtype_int;
					else if (innerIdx == 1) return VR::paramtype_vector;
					else if (innerIdx == 2) return VR::paramtype_int;
					return VR::paramtype_unspecified;
				}
			}
			if (index==0) return VR::paramtype_int;
			if (index==1) return VR::paramtype_vector;
			if (index==2) return VR::paramtype_int;
		}
		return VR::paramtype_unspecified;
	}

	/// Returns the list of mapping channels, which can be modified directly.
	AbcMapChannelsList* getMapChannels(double time) {
		int keyframeIdx=getKeyframeIndex(time);
		if (keyframeIdx==-1)
			return NULL;

		return &(keyframes[keyframeIdx].data);
	}

	void reserve(int count, double time) {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return;

		if (level == 0) {
			mapChannels->setCount(count, true);
			mapChannels->clear();
		} else if (level==2) {
			if(innerIdx == 1) {
				(*mapChannels)[chanIdx].verts.setCount(count, true);
				(*mapChannels)[chanIdx].verts.clear();
			}
			else if (innerIdx==2) {
				(*mapChannels)[chanIdx].faces.setCount(count, true);
				(*mapChannels)[chanIdx].faces.clear();
			}
		}
	}

	void setInt(int value, int index, double time) {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return;

		if (level == 1) {
			(*mapChannels)[chanIdx].idx = value;
		} else if (level==2) {
			if (innerIdx==2) {
				if(index>=0 && index<(*mapChannels)[chanIdx].faces.count()) (*mapChannels)[chanIdx].faces[index] = value;
				else (*mapChannels)[chanIdx].faces[index]+=value;
			}
		}
	}

	void setVector(const VR::Vector &value, int index, double time) {
		AbcMapChannelsList *mapChannels=getMapChannels(time);
		if (!mapChannels)
			return;

		if (level == 2 && innerIdx == 1) {
			if(index >= 0 && index < (*mapChannels)[chanIdx].verts.count()) (*mapChannels)[chanIdx].verts[index] = value;
			else (*mapChannels)[chanIdx].verts[index] += value;
		}
	}

protected:
	int chanIdx;
	int innerIdx;
	int level;
};

/// Information about a GeomStaticMesh plugin created for each object from the Alembic file.
struct AlembicMeshSource {
	VR::VRayPlugin *geomStaticMesh; ///< The GeomStaticMesh plugin.

	AnimatedVectorListParam verticesParam; ///< The parameter for the vertices.
	AnimatedIntListParam facesParam; ///< The parameter for the faces.

	AnimatedVectorListParam normalsParam; ///< The parameter for the normals.
	AnimatedIntListParam faceNormalsParam; ///< The parameter for the face normals.

	AnimatedVectorListParam velocitiesParam; ///< The parameter for the velocities.

	AnimatedMapChannelsParam mapChannelsParam; ///< Parameter for UV/color sets.

	AnimatedStringListParam mapChannelNamesParam; ///< A parameter with the map channel names.

	/// Parameter for the dynamic_geometry flag of the GeomStaticMesh plugin. Enabling dynamic
	/// geometry allows efficient instancing of the mesh geometry. Otherwise it is replicated
	/// for each instance. For now we always set this flag to true, although potentially this
	/// can be optimized.
	VR::DefBoolParam dynamicGeometryParam;

	int nsamples; ///< Number of time samples.

	/// Constructor.
	AlembicMeshSource(void):
		verticesParam("vertices"),
		facesParam("faces"),
		normalsParam("normals"),
		faceNormalsParam("faceNormals"),
		velocitiesParam("velocities"),
		geomStaticMesh(NULL),
		mapChannelsParam("map_channels"),
		mapChannelNamesParam("map_channels_names"),
		dynamicGeometryParam("dynamic_geometry", true),
		nsamples(1)
	{}

	void setNumTimeSteps(int numTimeSteps) {
		nsamples=numTimeSteps;
		verticesParam.reserveKeyframes(nsamples);
		facesParam.reserveKeyframes(nsamples);
		normalsParam.reserveKeyframes(nsamples);
		faceNormalsParam.reserveKeyframes(nsamples);
		velocitiesParam.reserveKeyframes(nsamples);
		mapChannelsParam.reserveKeyframes(nsamples);
		mapChannelNamesParam.reserveKeyframes(nsamples);
	}
};

/// Information about an instance of an AlembicMeshSource.
struct AlembicMeshInstance {
	AlembicMeshSource *meshSource; ///< The original mesh.
	VR::Transform tm; ///< The transformation matrix.
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
		addParamString("mtl_assignments_file", "", -1, "An optional XML file that controls material assignments", "fileAsset=(xml), fileAssetNames=(XML control file), fileAssetOp=(load)");
	}
};

/// The GeomAlembicReader plugin.
/// It reads the alembic file and creates a GeomStaticMesh via the SceneModifierInterface for every mesh in the alembic file.
/// This plugin is meant to be put in one or more Node plugins. For every such Node plugin,
/// GeomAlembicReader will create a GeomAlembicReaderInstance class, which does the job of compiling the geometry of the
/// GeomStaticMesh plugins with the Node transform and the appropriate material.
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
	VR::VRayStaticGeometry* newInstance(
		VR::MaterialInterface *mtl,
		VR::BSDFInterface *bsdf,
		int renderID,
		VR::VolumetricInterface *volume,
		VR::LightList *lightList,
		const VR::Transform &baseTM,
		int objectID,
		const tchar *userAttr,
		int primaryVisibility
	) VRAY_OVERRIDE;
	void deleteInstance(VR::VRayStaticGeometry *instance) VRAY_OVERRIDE;

	// From VRayPlugin
	void frameBegin(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where the GeomStaticMesh plugins will be created
	void frameEnd(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where we destroy our geometry plugins

	// From VRaySceneModifierInterface
	void preRenderBegin(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where the new material plugins will be created
	void postRenderEnd(VR::VRayRenderer *vray) VRAY_OVERRIDE; // This is where we destroy our material plugins

private:
	friend struct GeomAlembicReaderInstance;

	// Cached parameters
	VR::CharString fileName;
	VR::CharString mtlDefsFileName;
	VR::CharString mtlAssignmentsFileName;

	/// A default material for shading objects without material assignment.
	VR::VRayPlugin *defaultMtl;

	/// The mesh plugins that will be instanced for rendering.
	VR::Table<AlembicMeshSource*, -1> meshSources;

	/// The instances that will get rendered. This list will be populated by GeomAlembicReaderInstance.
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
	/// @param meshSets Information about the UV and color sets in the Alembic file. Used to fill in the names of the mapping channels.
	/// @retval The resulting AlembicMeshSource object. May be NULL if the object cannot be created.
	AlembicMeshSource *createGeomStaticMesh(VR::VRayRenderer *vray, VR::MeshFile &abcFile, int voxelIndex, int createInstance, VR::DefaultMeshSetsData &meshSets, int nsamples, double frameStart, double frameEnd);

	/// Create a default material to use for shading when no material assignment is found for an object.
	VRayPlugin* createDefaultMaterial(void);

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
