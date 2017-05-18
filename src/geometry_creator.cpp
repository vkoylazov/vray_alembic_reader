#include "geomalembicreader.h"

using namespace VR;

int isValidMappingChannel(const MeshChannel &chan) {
	if (chan.channelID<VERT_TEX_CHANNEL0 || chan.channelID>=VERT_TEX_TOPO_CHANNEL0)
		return false;
	return true;
}

AlembicMeshSource* GeomAlembicReader::createGeomStaticMesh(VRayRenderer *vray, MeshFile &abcFile, MeshVoxel &voxel, int createInstance, DefaultMeshSetsData &meshSets) {
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

	meshPlugin->setParameter(&abcMeshSource->dynamicGeometryParam);
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

	// Read the UV/color sets
	int numMapChannels=0;
	for (int i=0; i<voxel.numChannels; i++) {
		const MeshChannel &chan=voxel.channels[i];
		if (isValidMappingChannel(chan))
			numMapChannels++;
	}

	if (numMapChannels>0) {
		DefMapChannelsParam::MapChannelList &mapChannelsList=abcMeshSource->mapChannelsParam.getMapChannels();
		mapChannelsList.setCount(numMapChannels);

		int idx=0;
		for (int chanIdx=0; chanIdx<voxel.numChannels; chanIdx++) {
			const MeshChannel &chan=voxel.channels[chanIdx];
			if (isValidMappingChannel(chan)) {
				DefMapChannelsParam::MapChannel &mapChannel=mapChannelsList[idx];

				mapChannel.idx=chan.channelID-VERT_TEX_CHANNEL0;

				mapChannel.verts.setCount(chan.numElements);
				const VertGeomData *uvw=static_cast<const VertGeomData*>(chan.data);
				int numUVWs=chan.numElements;
				for (int j=0; j<numUVWs; j++) {
					mapChannel.verts[j]=uvw[j];
				}

				const MeshChannel *topoChan=voxel.getChannel(chan.depChannelID);
				if (topoChan) {
					const FaceTopoData *uvwFaces=static_cast<FaceTopoData*>(topoChan->data);
					int numUVWFaces=topoChan->numElements;

					mapChannel.faces.setCount(numUVWFaces*3);
					for (int j=0; j<numUVWFaces; j++) {
						const FaceTopoData &face=uvwFaces[j];
						int idx=j*3;
						mapChannel.faces[idx+0]=face.v[0];
						mapChannel.faces[idx+1]=face.v[1];
						mapChannel.faces[idx+2]=face.v[2];
					}
				}

				idx++;
			}
		}

		meshPlugin->setParameter(&abcMeshSource->mapChannelsParam);

		// Fill in the mapping channel names
		abcMeshSource->mapChannelNamesParam.setCount(numMapChannels, 0.0f);
		int numUVSets=meshSets.getNumSets(MeshSetsData::meshSetType_uvSet);
		for (int i=0; i<mapChannelsList.count(); i++) {
			const tchar *setName=NULL;
			if (i<numUVSets) setName=meshSets.getSetName(MeshSetsData::meshSetType_uvSet, i);
			else setName=meshSets.getSetName(MeshSetsData::meshSetType_colorSet, i-numUVSets);

			if (NULL==setName) setName="";
			abcMeshSource->mapChannelNamesParam.setString(setName, i, 0.0f);
		}

		meshPlugin->setParameter(&abcMeshSource->mapChannelNamesParam);
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
