/*
    Copyright (c) 2008 NetAllied Systems GmbH

    This file is part of COLLADAMaya.

    Portions of the code are:
    Copyright (c) 2005-2007 Feeling Software Inc.
    Copyright (c) 2005-2007 Sony Computer Entertainment America
    Copyright (c) 2004-2005 Alias Systems Corp.

    Licensed under the MIT Open Source License, 
    for details please see LICENSE file or the website
    http://www.opensource.org/licenses/mit-license.php
*/

#include "COLLADAMayaStableHeaders.h"
#include "COLLADAMayaGeometryImporter.h"
#include "ColladaMayaException.h"
#include "COLLADAMayaVisualSceneImporter.h"

#include <maya/MFnMesh.h>
#include <maya/MFnTransform.h>
#include <maya/MDagModifier.h>

#pragma warning(disable:4172)

#include "MayaDMTransform.h"
#include "MayaDMCommands.h"

#include "COLLADAFWPolygons.h"
#include "COLLADAFWTrifans.h"
#include "COLLADAFWTristrips.h"
#include "COLLADAFWEdge.h"


namespace COLLADAMaya
{
    
    const String GeometryImporter::GEOMETRY_NAME = "Geometry";
    const String GeometryImporter::GROUPID_NAME = "GroupId";


    // --------------------------------------------
    GeometryImporter::GeometryImporter( DocumentImporter* documentImporter ) 
    : BaseImporter ( documentImporter )
    {}

    // --------------------------------------------
    void GeometryImporter::importGeometry ( const COLLADAFW::Geometry* geometry )
    {
        if ( geometry == 0 ) return;

        // Check if the current geometry is already imported.
        const COLLADAFW::UniqueId& geometryId = geometry->getUniqueId ();
        if ( findMayaMeshNode ( geometryId ) != 0 ) return;

        COLLADAFW::Geometry::GeometryType type = geometry->getType ();
        switch ( type )
        {
        case COLLADAFW::Geometry::GEO_TYPE_CONVEX_MESH:
            std::cerr << "Import of convex_mesh not supported!" << std::endl;
            MGlobal::displayError ( "Import of convex_mesh not supported!" );
            return;
        case COLLADAFW::Geometry::GEO_TYPE_SPLINE:
            std::cerr << "Import of spline not supported!" << std::endl;
            MGlobal::displayError ( "Import of spline not supported!" );
            return;
        case COLLADAFW::Geometry::GEO_TYPE_MESH:
            {
                COLLADAFW::Mesh* mesh = ( COLLADAFW::Mesh* ) geometry;
                importMesh ( mesh );
                break;
            }
        default:
            return;
        }

        return;
    }

    // --------------------------------------------
    void GeometryImporter::importMesh ( const COLLADAFW::Mesh* mesh )
    {
        // Get the unique framework mesh id 
        const COLLADAFW::UniqueId& geometryId = mesh->getUniqueId ();

        // Get the transform node of the current mesh.
        VisualSceneImporter* visualSceneImporter = getDocumentImporter ()->getVisualSceneImporter ();

        // Get all visual scene nodes, which use this geometry and make the parent connections.
        const UniqueIdVec* transformNodes = visualSceneImporter->findGeometryTransformIds ( geometryId );
        size_t numNodeInstances = transformNodes->size ();

        // The index value of the current geometry instance.
        size_t geometryInstanceIndex = 0;

        UniqueIdVec::const_iterator nodesIter = transformNodes->begin ();
        while ( nodesIter != transformNodes->end () )
        {
            // Get the maya node of the current transform node.
            const COLLADAFW::UniqueId& transformNodeId = *nodesIter;
            MayaNode* mayaTransformNode = visualSceneImporter->findMayaTransformNode ( transformNodeId );
            String transformNodeName = mayaTransformNode->getName ();

            // Get the path to the parent transform node.
            String transformNodePath = mayaTransformNode->getNodePath ();

            // The first reference is a direct one, the others are instances.
            if ( nodesIter == transformNodes->begin() )
            {
                // Create the current mesh node.
                createMesh ( mesh, mayaTransformNode, numNodeInstances );
            }
            else
            {
                // Get the path to the mesh.
                MayaNode* mayaMeshNode = findMayaMeshNode ( geometryId );
                String meshNodePath = mayaMeshNode->getNodePath ();

                // parent -shape -noConnections -relative -addObject "|pCube1|pCubeShape1" "pCube2";
                FILE* file = getDocumentImporter ()->getFile ();
                MayaDM::parentShape ( file, meshNodePath, transformNodePath, false, true, true, true );
            }

            // Create maya group ids for every mesh primitive (if there is more than one).
            createGroupNodes ( mesh, geometryInstanceIndex );

            ++nodesIter;
            ++geometryInstanceIndex;
        }
    }

    // --------------------------------------------
    void GeometryImporter::createGroupNodes ( 
        const COLLADAFW::Mesh* mesh, 
        const size_t geometryInstanceIndex )
    {
        // Get the unique id of the current geometry.
        const COLLADAFW::UniqueId& geometryId = mesh->getUniqueId ();

        // We have to go through every mesh primitive.
        const COLLADAFW::MeshPrimitiveArray& meshPrimitives = mesh->getMeshPrimitives ();
        size_t meshPrimitivesCount = meshPrimitives.getCount ();

        // We don't need to create groups if we just have one primitive.
        if ( meshPrimitivesCount <= 1 ) return;

        // Create a group for every primitive.
        for ( size_t primitiveIndex=0; primitiveIndex<meshPrimitivesCount; ++primitiveIndex )
        {
            String groupName ( GROUPID_NAME );
            groupName = mGroupIdList.addId ( groupName );
            FILE* file = getDocumentImporter ()->getFile ();
            MayaDM::GroupId groupId ( file, groupName );

            // Assign the group to the unique geometry id, the transform node 
            // to the mesh instance and the index of the geometry's primitives.
            GroupIdAssignment groupIdAssignment ( groupId, geometryId, geometryInstanceIndex, primitiveIndex );
            mGroupIdAssignments.push_back ( groupIdAssignment );
        }
    }

    // --------------------------------------------
    void GeometryImporter::createMesh ( 
        const COLLADAFW::Mesh* mesh, 
        MayaNode* mayaTransformNode, 
        size_t numNodeInstances )
    {
        // Create a unique name.
        String meshName = mesh->getName ();
        if ( COLLADABU::Utils::equals ( meshName, "" ) ) 
            meshName = GEOMETRY_NAME;
        meshName = mMeshNodeIdList.addId ( meshName );

        // Create a maya node object of the current node and push it into the map.
        const COLLADAFW::UniqueId& uniqueId = mesh->getUniqueId ();
        MayaNode mayaMeshNode ( uniqueId, meshName, mayaTransformNode );
        mMayaMeshNodesMap [ uniqueId ] = mayaMeshNode;

        // Get the parent node name.
        if ( mayaTransformNode == NULL ) 
        {
            assert ( mayaTransformNode != NULL );
            MGlobal::displayError ( "No transform node! ");
            return;
        }
        String transformNodePath = mayaTransformNode->getNodePath ();

        // Create the current mesh node.
        FILE* file = getDocumentImporter ()->getFile ();
        MayaDM::Mesh meshNode ( file, meshName, transformNodePath );
        mMayaDMMeshNodesMap [uniqueId] = meshNode;

        // Writes the object groups for every mesh primitive and
        // gets all shader engines, which are used by the primitive elements of the mesh.
        writeObjectGroups ( mesh, meshNode, numNodeInstances );

        // Write the vertex positions. 
        // Just write the values, they will be referenced from the edges and the faces.
        writeVertexPositions ( mesh, meshNode );

        // Write the normals. 
        writeNormals ( mesh, meshNode );

        // Write the uv corrdinates.
        writeUVSets ( mesh, meshNode );

        // TODO Implementation: set current uv set
//        writeCurrentUVSet ( mesh, meshNode );

        // Write the uv corrdinates.
        writeColorSets ( mesh, meshNode );

        // The vector of edge indices. We use it to write the list of edges into 
        // the maya file. The vector is already sorted.
        std::vector<COLLADAFW::Edge> edgeIndices;

        // We store the edge indices also in a sorted map. The dublicate data holding 
        // is reasonable, because we need the index of a given edge. The search of  
        // values in a map is much faster than in a vector!
        std::map<COLLADAFW::Edge,size_t> edgeIndicesMap;

        // Iterates over the mesh primitives and reads the edge indices.
        getEdgeIndices ( mesh, edgeIndices, edgeIndicesMap );

        // Write the edge indices of all primitive elements into the maya file.
        writeEdges ( edgeIndices, meshNode );

        // Write the face informations of all primitive elements into the maya file.
        writeFaces ( mesh, edgeIndicesMap, meshNode );

        // Fills the ShadingEnginePrimitivesMap. Used to create the connections between the 
        // shading engines and the geometries.
        setMeshPrimitiveShadingEngines ( mesh );
    }

    // --------------------------------------------
    void GeometryImporter::writeObjectGroups ( 
        const COLLADAFW::Mesh* mesh, 
        MayaDM::Mesh &meshNode, 
        size_t numNodeInstances )
    {
        // Create the object group instances and the object groups and write it into the maya file.

        // setAttr -size 2 ".instObjGroups"; // for every instance
        // setAttr -size 2 ".instObjGroups[0].objectGroups"; // for every mesh primitive
        // setAttr ".instObjGroups[0].objectGroups[0].objectGrpCompList" -type "componentList" 1 "f[0:5]";
        // setAttr ".instObjGroups[0].objectGroups[1].objectGrpCompList" -type "componentList" 1 "f[6:11]";

        // We have to go through every mesh primitive.
        const COLLADAFW::MeshPrimitiveArray& meshPrimitives = mesh->getMeshPrimitives ();
        size_t meshPrimitivesCount = meshPrimitives.getCount ();

        // We don't need this, if we have just one primitive.
        if ( meshPrimitivesCount <= 1 ) return;

        // Iterate over the object instances.
        for ( size_t j=0; j<numNodeInstances; ++j )
        {
            size_t initialFaceIndex = 0;

            // Iterate over the mesh primitives
            for ( size_t i=0; i<meshPrimitivesCount; ++i )
            {
                // Get the number of faces of the current primitive element.
                const COLLADAFW::MeshPrimitive* meshPrimitive = meshPrimitives [ i ];
                // TODO Is this also with trifans, etc. the right number???
                size_t numFaces = meshPrimitive->getGroupedVertexElementsCount ();
//                 COLLADAFW::Trifans* trifans = (COLLADAFW::Trifans*) primitiveElement;
//                 COLLADAFW::Trifans::VertexCountArray& vertexCountArray = 
//                     trifans->getGroupedVerticesVertexCountArray ();
//                 meshPrimitive->getFaceCount ();
//                 meshPrimitive->getGroupedVerticesVertexCount ();

                // Create the string with the face informations for the component list.
                String val = "f[" + COLLADABU::Utils::toString ( initialFaceIndex ) 
                    + ":" + COLLADABU::Utils::toString ( numFaces-1 + initialFaceIndex ) + "]";

                // Create the component list.
                MayaDM::componentList componentList;
                componentList.push_back ( val );

                // Increment the initial face index.
                initialFaceIndex += numFaces;

                // Write instance object group component list data into the file.
                meshNode.setObjectGrpCompList ( j, i, componentList );
            }
        }
    }

    // --------------------------------------------
    void GeometryImporter::setMeshPrimitiveShadingEngines ( const COLLADAFW::Mesh* mesh )
    {
        const COLLADAFW::UniqueId& geometryId = mesh->getUniqueId ();

        // We have to go through every mesh primitive.
        const COLLADAFW::MeshPrimitiveArray& meshPrimitives = mesh->getMeshPrimitives ();
        size_t meshPrimitivesCount = meshPrimitives.getCount ();

        for ( size_t primitiveIndex=0; primitiveIndex<meshPrimitivesCount; ++primitiveIndex )
        {
            // Get the current primitive element.
            const COLLADAFW::MeshPrimitive* meshPrimitive = meshPrimitives [ primitiveIndex ];

            // Get the shader engine id.
            String shadingEngineName = meshPrimitive->getMaterial ();
            COLLADAFW::MaterialId shadingEngineId = meshPrimitive->getMaterialId ();

            // Fills the ShadingEnginePrimitivesMap. Used to create the connections between the 
            // shading engines and the geometries.
            // The map holds for every geometry's shading engine a list of the index values of the 
            // geometry's primitives. 
            setShadingEnginePrimitiveIndex ( geometryId, shadingEngineId, primitiveIndex );
        }
    }

    // --------------------------------------------
    void GeometryImporter::writeVertexPositions ( 
        const COLLADAFW::Mesh* mesh, 
        MayaDM::Mesh &meshNode )
    {
        // We have always a stride of three (x, y and z values)
        size_t stride = 3;

        const COLLADAFW::MeshVertexData& positions = mesh->getPositions ();
        const COLLADAFW::MeshVertexData::DataType type = positions.getType ();
        switch ( type )
        {
        case COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT:
            {
                const COLLADAFW::ArrayPrimitiveType<float>* values = positions.getFloatValues ();
                size_t count = positions.getValuesCount ();
                meshNode.startVrts ( 0, (count/stride)-1 ); 
                for ( size_t i=0, index=0; i<count; i+=stride, ++index )
                {
                    COLLADABU::Math::Vector3 converted;
                    toLinearUnit ( (*values)[i], (*values)[i+1], (*values)[i+2], converted );
                    meshNode.appendVrts ( (float)converted[0] );
                    meshNode.appendVrts ( (float)converted[1] );
                    meshNode.appendVrts ( (float)converted[2] );
                }
                meshNode.endVrts (); 
            }
            break;
        case COLLADAFW::MeshVertexData::DATA_TYPE_DOUBLE:
            {
                const COLLADAFW::ArrayPrimitiveType<double>* values = positions.getDoubleValues ();
                size_t count = positions.getValuesCount ();
                meshNode.startVrts ( 0, (count/stride)-1 ); 
                for ( size_t i=0, index=0; i<count; i+=stride, ++index )
                {
                    COLLADABU::Math::Vector3 converted;
                    toLinearUnit ( (*values)[i], (*values)[i+1], (*values)[i+2], converted );
                    meshNode.appendVrts ( (float)converted[0] );
                    meshNode.appendVrts ( (float)converted[1] );
                    meshNode.appendVrts ( (float)converted[2] );
                }
                meshNode.endVrts (); 
            }
            break;
        default:
            std::cerr << "No valid data type for positions: " << type << std::endl;
            MGlobal::displayError ( "No valid data type for positions: " + type );
            assert ( "No valid data type for positions: " + type );
        }
    }

    // --------------------------------------------
    void GeometryImporter::writeNormals ( const COLLADAFW::Mesh* mesh, MayaDM::Mesh &meshNode )
    {
        // Count the number of normals to write into the maya file.
        size_t numNormals = mesh->getNormalsCount ();

        // Write the normals into the maya file.
        if ( numNormals > 0 )
        {
            meshNode.startNormals ( 0, numNormals-1 ); 
            appendNormalValues ( mesh, meshNode );
            meshNode.endNormals (); 
        }
    }

    // --------------------------------------------
    void GeometryImporter::appendNormalValues ( 
        const COLLADAFW::Mesh* mesh, 
        MayaDM::Mesh &meshNode )
    {
        // Get the mesh normals values.
        const COLLADAFW::MeshVertexData& normals = mesh->getNormals ();

        size_t stride = 3; // x, y, z

        // We have to go through every mesh primitive and append every element. 
        const COLLADAFW::MeshPrimitiveArray& meshPrimitives = mesh->getMeshPrimitives ();
        size_t count = meshPrimitives.getCount ();
        for ( size_t i=0; i<count; ++i )
        {
            // Get the current primitive element.
            const COLLADAFW::MeshPrimitive* meshPrimitive = meshPrimitives [ i ];

            // Get the normal indices of the current primitive.
            const COLLADAFW::UIntValuesArray& normalIndices = meshPrimitive->getNormalIndices ();

            // Iterate over the indices and write their normal values into the maya file.
            size_t indexCount = normalIndices.getCount ();
            for ( size_t j=0; j<indexCount; ++j )
            {
                // Get the index of the current normal.
                unsigned int normalIndex = normalIndices [ j ];

                // Get the position in the values list to read.
                unsigned int pos = normalIndex * stride;

                // Write the normal values on the index values.
                const COLLADAFW::MeshVertexData::DataType type = normals.getType ();
                switch ( type )
                {
                case COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT:
                    {
                        const COLLADAFW::ArrayPrimitiveType<float>* values = normals.getFloatValues ();
                        meshNode.appendNormals ( (*values)[pos] );
                        meshNode.appendNormals ( (*values)[pos+1] );
                        meshNode.appendNormals ( (*values)[pos+2] );
                    }
                    break;
                case COLLADAFW::MeshVertexData::DATA_TYPE_DOUBLE:
                    {
                        const COLLADAFW::ArrayPrimitiveType<double>* values = normals.getDoubleValues ();
                        meshNode.appendNormals ( (float)(*values)[pos] );
                        meshNode.appendNormals ( (float)(*values)[pos+1] );
                        meshNode.appendNormals ( (float)(*values)[pos+2] );
                    }
                    break;
                default:
                    std::cerr << "No valid data type for normals: " << type << std::endl;
                    MGlobal::displayError ( "No valid data type for normals: " + type );
                    assert ( "No valid data type for normals: " + type );
                }
            }
        }
    }

    // --------------------------------------------
    void GeometryImporter::writeUVSets ( const COLLADAFW::Mesh* mesh, MayaDM::Mesh &meshNode )
    {
        // Set the number of uv sets.
        const COLLADAFW::MeshVertexData& uvCoords = mesh->getUVCoords ();
        size_t sumUVSetPoints = uvCoords.getNumInputInfos ();
        if ( sumUVSetPoints == 0 ) return;
        meshNode.setUvSize ( sumUVSetPoints );

        // Write the values 
        size_t initialIndex = 0;
        const COLLADAFW::MeshVertexData::DataType type = uvCoords.getType ();
        switch ( type )
        {
        case COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT:
            {
                const COLLADAFW::ArrayPrimitiveType<float>* values = uvCoords.getFloatValues ();
                for ( size_t i=0; i<sumUVSetPoints; ++i )
                {
                    meshNode.setUvSetName ( i, uvCoords.getName ( i ) );
                    
                    size_t stride = uvCoords.getStride ( i );
                    assert ( stride > 1 && stride <= 4 );
                    if ( stride != 2 ) 
                        MGlobal::displayWarning ( "Just 2d uv set data will be imported! ");

                    size_t indicesCount = uvCoords.getLength ( i );
                    meshNode.startUvSetPoints ( i, 0, (indicesCount/stride)-1 ); 

                    unsigned int index = 0;
                    for ( size_t i=0; i<indicesCount; i+=stride )
                    {
                        meshNode.appendUvSetPoints ( (*values)[initialIndex+i] );
                        meshNode.appendUvSetPoints ( (*values)[initialIndex+i+1] );
                    }
                    meshNode.endUvSetPoints (); 

                    initialIndex += indicesCount;
                }
                break;
            }
        case COLLADAFW::MeshVertexData::DATA_TYPE_DOUBLE:
            {
                const COLLADAFW::ArrayPrimitiveType<double>* values = uvCoords.getDoubleValues ();
                for ( size_t i=0; i<sumUVSetPoints; ++i )
                {
                    meshNode.setUvSetName ( i, uvCoords.getName ( i ) );

                    size_t stride = uvCoords.getStride ( i );
                    assert ( stride > 1 && stride <= 4 );
                    if ( stride != 2 ) 
                        MGlobal::displayWarning ( "Just 2d uv set data will be imported! ");

                    size_t indicesCount = uvCoords.getLength ( i );
                    meshNode.startUvSetPoints ( i, 0, (indicesCount/stride)-1 ); 

                    unsigned int index = 0;
                    for ( size_t i=0; i<indicesCount; i+=stride )
                    {
                        meshNode.appendUvSetPoints ( (float) (*values)[initialIndex+i] );
                        meshNode.appendUvSetPoints ( (float) (*values)[initialIndex+i+1] );
                    }
                    meshNode.endUvSetPoints (); 

                    initialIndex += indicesCount;
                }
            }
            break;
        default:
            std::cerr << "No valid data type for uv coordinates: " << type << std::endl;
            MGlobal::displayError ( "No valid data type for uv coordinates: " + type );
            assert ( "No valid data type for uv coordinates: " + type );
        }
    }

    // --------------------------------------------
    void GeometryImporter::writeColorSets ( const COLLADAFW::Mesh* mesh, MayaDM::Mesh &meshNode )
    {
        // Set the number of uv sets.
        const COLLADAFW::MeshVertexData& colors = mesh->getColors ();
        size_t sumColorSetPoints = colors.getNumInputInfos ();
        if ( sumColorSetPoints == 0 ) return;
//        meshNode.setColorSetSize ( sumUVSetPoints );

        // Write the values 
        size_t initialIndex = 0;
        const COLLADAFW::MeshVertexData::DataType type = colors.getType ();
        switch ( type )
        {
        case COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT:
            {
                const COLLADAFW::ArrayPrimitiveType<float>* values = colors.getFloatValues ();
                for ( size_t i=0; i<sumColorSetPoints; ++i )
                {
                    meshNode.setColorName ( i, colors.getName ( i ) );

                    size_t stride = colors.getStride ( i );
                    assert ( stride == 1 || stride == 3 || stride <= 4 );

                    unsigned int representation = 2; // RGBA = 2 DEFAULT
                    if ( stride == 1 ) representation = 1; // A = 1
                    else if ( stride == 3 ) representation = 3; // RGB = 3
                    meshNode.setRepresentation ( i, representation );

                    size_t indicesCount = colors.getLength ( i );
                    meshNode.startColorSetPoints ( i, 0, (indicesCount/stride)-1 ); 

                    unsigned int index = 0;
                    for ( size_t i=0; i<indicesCount; i+=stride )
                    {
                        for ( size_t j=0; j<stride; ++j ) 
                        {
                            meshNode.appendColorSetPoints ( (*values)[initialIndex+i+j] );
                        }
                    }
                    meshNode.endColorSetPoints (); 

                    initialIndex += indicesCount;
                }
                break;
            }
        case COLLADAFW::MeshVertexData::DATA_TYPE_DOUBLE:
            {
                const COLLADAFW::ArrayPrimitiveType<double>* values = colors.getDoubleValues ();
                for ( size_t i=0; i<sumColorSetPoints; ++i )
                {
                    meshNode.setColorName ( i, colors.getName ( i ) );

                    size_t stride = colors.getStride ( i );
                    assert ( stride == 1 || stride == 3 || stride <= 4 );

                    unsigned int representation = 2; // RGBA = 2 DEFAULT
                    if ( stride == 1 ) representation = 1; // A = 1
                    else if ( stride == 3 ) representation = 3; // RGB = 3
                    meshNode.setRepresentation ( i, representation );

                    size_t indicesCount = colors.getLength ( i );
                    meshNode.startColorSetPoints ( i, 0, (indicesCount/stride)-1 ); 

                    unsigned int index = 0;
                    for ( size_t i=0; i<indicesCount; i+=stride )
                    {
                        for ( size_t j=0; j<stride; ++j ) 
                        {
                            meshNode.appendColorSetPoints ( ( float ) (*values)[initialIndex+i+j] );
                        }
                    }
                    meshNode.endColorSetPoints (); 

                    initialIndex += indicesCount;
                }
            }
            break;
        default:
            std::cerr << "No valid data type for colors: " << type << std::endl;
            MGlobal::displayError ( "No valid data type for colors: " + type );
            assert ( "No valid data type for colors: " + type );
        }
    }

    // --------------------------------------------
    void GeometryImporter::writeEdges (  
        const std::vector<COLLADAFW::Edge> &edgeIndices, 
        MayaDM::Mesh &meshNode )
    {
        size_t numEdges = edgeIndices.size ();
        if ( numEdges > 0 )
        {
            // We tell allways, that we have hard edges, so every vertex has a normal.
            int edgh = 0;

            // Go through the edges and write them
            meshNode.startEdge ( 0, numEdges-1 );
            for ( size_t k=0; k<numEdges; ++k )
            {
                const COLLADAFW::Edge& edge = edgeIndices [k];
                meshNode.appendEdge ( edge[0] );
                meshNode.appendEdge ( edge[1] );
                meshNode.appendEdge ( edgh );
            }
            meshNode.endEdge ();
        }
    }

    // --------------------------------------------
    void GeometryImporter::writeFaces ( 
        const COLLADAFW::Mesh* mesh, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        MayaDM::Mesh &meshNode )
    {
        // Get the number of faces in the current mesh.
        size_t numFaces = mesh->getFacesCount ();
        if ( numFaces <= 0 ) return;

        // Start to write the faces into the maya file
        meshNode.startFace ( 0, numFaces-1 );

        // Go through the primitive elements and write the face values.
        const COLLADAFW::MeshPrimitiveArray& primitiveElementsArray = mesh->getMeshPrimitives ();
        size_t count = primitiveElementsArray.getCount ();

        // Determine the face values.
        for ( size_t i=0; i<count; ++i )
        {
            // Get the primitive element.
            COLLADAFW::MeshPrimitive* primitiveElement = primitiveElementsArray [ i ];

            // Write the face informations into the maya file.
            COLLADAFW::MeshPrimitive::PrimitiveType primitiveType = primitiveElement->getPrimitiveType ();
            switch ( primitiveType )
            {
            case COLLADAFW::MeshPrimitive::TRIANGLE_FANS:
                appendTrifansPolyFaces ( mesh, primitiveElement, edgeIndicesMap, meshNode );
                break;
            case COLLADAFW::MeshPrimitive::TRIANGLE_STRIPS:
                appendTristripsPolyFaces ( mesh, primitiveElement, edgeIndicesMap, meshNode );
                break;
            case COLLADAFW::MeshPrimitive::POLYGONS:
            case COLLADAFW::MeshPrimitive::POLYLIST:
            case COLLADAFW::MeshPrimitive::TRIANGLES:
                appendPolygonPolyFaces ( mesh, primitiveElement, edgeIndicesMap, meshNode );
                break;
            default:
                std::cerr << "Primitive type not implemented!" << std::endl;
                MGlobal::displayError ( "Primitive type not implemented!" );
                assert ( "Primitive type not implemented!");
            }
        }

        // End the face element.
        meshNode.endFace ();
    }

    // --------------------------------------------
    void GeometryImporter::appendTrifansPolyFaces ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        MayaDM::Mesh &meshNode )
    {
        // Get the position indices.
        const COLLADAFW::UIntValuesArray& positionIndices = primitiveElement->getPositionIndices ();

        // The points of an edge
        int edgeStartVtxIndex=0, edgeEndVtxIndex=0;

        // The current index in the positions list.
        size_t initialPositionIndex=0;
        size_t positionIndex=0;

        size_t uvSetIndicesIndex = 0;
        size_t colorIndicesIndex = 0;

        // Iterate over the grouped vertices and get the edges for every group.
        COLLADAFW::Trifans* trifans = (COLLADAFW::Trifans*) primitiveElement;
        COLLADAFW::Trifans::VertexCountArray& vertexCountArray = trifans->getGroupedVerticesVertexCountArray ();
        size_t groupedVertexElementsCount = vertexCountArray.getCount ();
        for ( size_t groupedVerticesIndex=0; groupedVerticesIndex<groupedVertexElementsCount; ++groupedVerticesIndex )
        {
            // Create the poly face
            MayaDM::polyFaces polyFace;

            // A trifan has always triangles, which have 3 edges
            size_t triangleEdgeCounter = 0;

            // The number of vertices in the current vertex group.
            unsigned int vertexCount = vertexCountArray [groupedVerticesIndex];

            // Determine the number of edges and iterate over it.
            unsigned int numEdges = ( vertexCount - 3 ) * 3 + 3;
            for ( unsigned int edgeIndex=0; edgeIndex<numEdges; ++edgeIndex )
            {
                if ( triangleEdgeCounter == 0 )
                {
                    // Handle the edge informations.
                    polyFace.f.faceEdgeCount = 3;
                    polyFace.f.edgeIdValue = new int[3];
                }

                // Increment the current triangle edge counter, so we know if we have the full triangle.
                ++triangleEdgeCounter;

                // Get the start edge index
                if ( triangleEdgeCounter > 1 )
                    edgeStartVtxIndex = positionIndices[positionIndex];
                else edgeStartVtxIndex = positionIndices[initialPositionIndex];

                // With the third edge of a triangle, we have to go back to the trifans root.
                if ( triangleEdgeCounter < 3 )
                    edgeEndVtxIndex = positionIndices[++positionIndex];
                else edgeEndVtxIndex = positionIndices[initialPositionIndex];

                // Set the edge vertex index values into an edge object.
                COLLADAFW::Edge edge ( edgeStartVtxIndex, edgeEndVtxIndex );

                // Variable for the current edge index.
                int edgeIndexValue;

                // Get the edge index value from the edge list.
                getEdgeIndex ( edge, edgeIndicesMap, edgeIndexValue );

                // Set the edge list index into the poly face
                polyFace.f.edgeIdValue[triangleEdgeCounter-1] = edgeIndexValue;

                // Reset the edge counter, if we have all three edges of a triangle.
                if ( triangleEdgeCounter == 3 ) 
                {
                    triangleEdgeCounter = 0;
                    --positionIndex;

                    // Handle the uv set infos.
                    setUVSetInfos ( mesh, primitiveElement, polyFace, uvSetIndicesIndex, 3 );
                    uvSetIndicesIndex -= 2;

                    // Handle the uv set infos.
                    setColorInfos ( mesh, primitiveElement, polyFace, colorIndicesIndex, 3 );
                    colorIndicesIndex -= 2;

                    // Write the polyFace data in the maya file.
                    meshNode.appendFace ( polyFace );
                }
            }

            // Increment the initial trifan position index for the next trifan object.
            positionIndex += 2;
            initialPositionIndex += vertexCount;

            uvSetIndicesIndex += 2;
            colorIndicesIndex += 2;
        }
    }

    // --------------------------------------------
    void GeometryImporter::appendTristripsPolyFaces ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        MayaDM::Mesh &meshNode )
    {
        // Get the position indices.
        const COLLADAFW::UIntValuesArray& positionIndices = primitiveElement->getPositionIndices ();

        // The points of an edge
        int edgeStartVtxIndex=0, edgeEndVtxIndex=0;

        // The current index in the positions list.
        size_t initialPositionIndex=0;
        size_t positionIndex=0;

        size_t uvSetIndicesIndex = 0;
        size_t colorIndicesIndex = 0;

        // Iterate over the grouped vertices and get the edges for every group.
        COLLADAFW::Trifans* trifans = (COLLADAFW::Trifans*) primitiveElement;
        COLLADAFW::Trifans::VertexCountArray& vertexCountArray = 
            trifans->getGroupedVerticesVertexCountArray ();
        size_t groupedVertexElementsCount = vertexCountArray.getCount ();
        for ( size_t groupedVerticesIndex=0; groupedVerticesIndex<groupedVertexElementsCount; ++groupedVerticesIndex )
        {
            // Create the poly face
            MayaDM::polyFaces polyFace;

            // A trifan has always triangles, which have 3 edges
            size_t triangleEdgeCounter = 0;

            // The number of vertices in the current vertex group.
            unsigned int vertexCount = vertexCountArray [groupedVerticesIndex];

            // Determine the number of edges and iterate over it.
            unsigned int numEdges = ( vertexCount - 3 ) * 3 + 3;
            for ( unsigned int edgeIndex=0; edgeIndex<numEdges; ++edgeIndex )
            {
                if ( triangleEdgeCounter == 0 )
                {
                    // Handle the edge informations.
                    polyFace.f.faceEdgeCount = 3;
                    polyFace.f.edgeIdValue = new int[3];
                }

                // Increment the current triangle edge counter, so we know if we have the full triangle.
                ++triangleEdgeCounter;

                // Get the start edge index
                edgeStartVtxIndex = positionIndices[positionIndex];

                // With the third edge of a triangle, we have to go back to the trifans root.
                if ( triangleEdgeCounter < 3 )
                    edgeEndVtxIndex = positionIndices[++positionIndex];
                else edgeEndVtxIndex = positionIndices[initialPositionIndex];

                // Set the edge vertex index values into an edge object.
                COLLADAFW::Edge edge ( edgeStartVtxIndex, edgeEndVtxIndex );

                // Variable for the current edge index.
                int edgeIndexValue;

                // Get the edge index value from the edge list.
                getEdgeIndex ( edge, edgeIndicesMap, edgeIndexValue );

                // Set the edge list index into the poly face
                polyFace.f.edgeIdValue[triangleEdgeCounter-1] = edgeIndexValue;

                // Reset the edge counter, if we have all three edges of a triangle.
                if ( triangleEdgeCounter == 3 ) 
                {
                    triangleEdgeCounter = 0;
                    --positionIndex;
                    initialPositionIndex = positionIndex;

                    // Handle the uv set infos.
                    setUVSetInfos ( mesh, primitiveElement, polyFace, uvSetIndicesIndex, 3 );
                    uvSetIndicesIndex -= 2;

                    // Handle the uv set infos.
                    setColorInfos ( mesh, primitiveElement, polyFace, colorIndicesIndex, 3 );
                    colorIndicesIndex -= 2;

                    // Write the polyFace data in the maya file.
                    meshNode.appendFace ( polyFace );
                }
            }

            // Increment the initial trifan position index for the next trifan object.
            positionIndex += 2;
            initialPositionIndex = positionIndex;

            uvSetIndicesIndex -= 2;
            colorIndicesIndex -= 2;
        }
    }

    // --------------------------------------------
    void GeometryImporter::appendPolygonPolyFaces ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        MayaDM::Mesh &meshNode )
    {
        size_t positionIndex=0;
        size_t uvSetIndicesIndex = 0;
        size_t colorIndicesIndex = 0;

        // The number of grouped vertex elements (faces, holes, tristrips or trifans).
        int groupedVerticesCount = primitiveElement->getGroupedVertexElementsCount ();
        if ( groupedVerticesCount < 0 ) groupedVerticesCount *= (-1);

        // To handle polygons with holes:
        // Flag, if the actual face is clockwise orientated. We need this information to handle
        // polygons with holes, because this need the opposite direction of their polygons. 
        bool faceClockwiseOriented = true;

        // Polygons with holes: we have always first one polygon for any number of holes.
        // We need the first three vertexes to determine the orientation of any polygon.
        std::vector<COLLADABU::Math::Vector3*> polygonPoints;
        
        // Iterate over all grouped vertex elements (faces, holes, tristrips or trifans)
        // and determine the values for the maya polyFace object.
        for ( int groupedVtxIndex=0; groupedVtxIndex<groupedVerticesCount; ++groupedVtxIndex )
        {
            // The number of edges is always the same than the number of vertices in the current 
            // grouped vertices object. If the number is negative, the grouped object is a hole.
            int numVertices = primitiveElement->getGroupedVerticesVertexCount ( (size_t)groupedVtxIndex );

            // Determine the number of edges 
            int numEdges = numVertices;

            // Create the poly face
            MayaDM::polyFaces polyFace;

            // Handle the face infos.
            if ( numEdges >= 0 )
                setPolygonFaceInfos ( mesh, primitiveElement, edgeIndicesMap, polyFace, numEdges, positionIndex, polygonPoints );
            else
                setPolygonHoleInfos ( mesh, primitiveElement, edgeIndicesMap, polyFace, numEdges, positionIndex, polygonPoints );

            // Handle the uv set infos.
            setUVSetInfos ( mesh, primitiveElement, polyFace, uvSetIndicesIndex, numEdges );

            // Handle the uv set infos.
            setColorInfos ( mesh, primitiveElement, polyFace, colorIndicesIndex, numEdges );

            // Write the polyFace data in the maya file.
            meshNode.appendFace ( polyFace );
        }

        // Delete the points.
        size_t pSize = polygonPoints.size ();
        for ( size_t i=0; i<pSize; ++i) 
            delete polygonPoints [i];
        polygonPoints.clear ();
    }

    // --------------------------------------------
    void GeometryImporter::setPolygonFaceInfos ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        MayaDM::polyFaces &polyFace, 
        int& numEdges, 
        size_t& positionIndex, 
        std::vector<COLLADABU::Math::Vector3*> &polygonPoints )
    {
        // Handle the edge informations.
        polyFace.f.faceEdgeCount = numEdges;
        polyFace.f.edgeIdValue = new int[numEdges];

        // Get the position indices
        const COLLADAFW::UIntValuesArray& positionIndices = primitiveElement->getPositionIndices ();

        // Go through the edges and determine the face values.
        for ( int edgeIndex=0; edgeIndex<numEdges; ++edgeIndex )
        {
            // Set the edge vertex index values into an edge object.
            int edgeStartVtxIndex = positionIndices[positionIndex];
            int edgeEndVtxIndex = 0;
            if ( edgeIndex<(numEdges-1) )
                edgeEndVtxIndex = positionIndices[++positionIndex];
            else edgeEndVtxIndex = positionIndices[positionIndex-numEdges+1];
            COLLADAFW::Edge edge ( edgeStartVtxIndex, edgeEndVtxIndex );

            // Polygons with holes: Get the first three polygon vertices to determine 
            // the polygon's orientation.
            COLLADAFW::MeshPrimitive::PrimitiveType primitiveType = primitiveElement->getPrimitiveType ();
            if ( primitiveType == COLLADAFW::MeshPrimitive::POLYGONS && edgeIndex < 3 )
            {
                // Delete the old points, if they still exist.
                if ( edgeIndex == 0 && polygonPoints.size () > 0 )
                {
                    // Delete the points.
                    size_t pSize = polygonPoints.size ();
                    for ( size_t i=0; i<pSize; ++i) 
                        delete polygonPoints [i];
                    polygonPoints.clear ();
                }
                // Store the vertex positions of the current start point.
                polygonPoints.push_back ( getVertexPosition ( mesh, edgeStartVtxIndex ) );
            }

            // Variable for the current edge index.
            int edgeIndexValue;

            // Get the edge index value from the edge list.
            getEdgeIndex ( edge, edgeIndicesMap, edgeIndexValue );

            // Set the edge list index into the poly face
            polyFace.f.edgeIdValue[edgeIndex] = edgeIndexValue;
        }

        // Increment the positions index for the next face
        ++positionIndex;
    }

    // --------------------------------------------
    void GeometryImporter::setPolygonHoleInfos ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        MayaDM::polyFaces &polyFace, 
        int &numEdges, 
        size_t &positionIndex, 
        std::vector<COLLADABU::Math::Vector3*> & polygonPoints )
    {
        // Get the position indices
        const COLLADAFW::UIntValuesArray& positionIndices = primitiveElement->getPositionIndices ();

        // Handle a hole element.
        numEdges *= -1;

        // The orientation of a hole has always to be the opposite direction of his
        // parenting polygon. About this, we have to determine the hole's orientation.
        // We just need the first three vectors to determine the polygon's orientation.
        std::vector<COLLADABU::Math::Vector3*> holePoints;

        polyFace.h.holeEdgeCount = numEdges;
        polyFace.h.edgeIdValue = new int[numEdges];

        // Go through the edges and determine the face values.
        for ( int edgeIndex=0; edgeIndex<numEdges; ++edgeIndex )
        {
            // Set the edge vertex index values into an edge object.
            int edgeStartVtxIndex = positionIndices[positionIndex];
            int edgeEndVtxIndex = 0;
            if ( edgeIndex<(numEdges-1) )
                edgeEndVtxIndex = positionIndices[++positionIndex];
            else edgeEndVtxIndex = positionIndices[positionIndex-numEdges+1];
            COLLADAFW::Edge edge ( edgeStartVtxIndex, edgeEndVtxIndex );

            // Polygons with holes: Get the first three polygon vertices to determine 
            // the polygon's orientation.
            if ( edgeIndex < 3 )
            {
                // Store the vertex positions of the current start point.
                holePoints.push_back ( getVertexPosition ( mesh, edgeStartVtxIndex ) );
            }

            // The current edge index.
            int edgeIndexValue;

            // Get the edge index value from the edge list.
            getEdgeIndex ( edge, edgeIndicesMap, edgeIndexValue );

            // Set the edge list index into the poly face
            polyFace.h.edgeIdValue[edgeIndex] = edgeIndexValue;
        }

        // Check if we have to change the orientation of the current hole.
        if ( changeHoleOrientation ( polygonPoints, holePoints ) )
        {
            changePolyFaceHoleOrientation ( polyFace );
        }

        // Delete the points.
        size_t hSize = holePoints.size ();
        for ( size_t i=0; i<holePoints.size (); ++i) 
            delete holePoints [i];
        holePoints.clear ();

        // Increment the positions index for the next face
        ++positionIndex;
    }

    // --------------------------------------------
    void GeometryImporter::setUVSetInfos ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        MayaDM::polyFaces &polyFace, 
        size_t& uvSetIndicesIndex, 
        const int numEdges )
    {
        const COLLADAFW::IndexListArray& uvSetIndicesArray = primitiveElement->getUVCoordIndicesArray ();
        size_t numUVSets = uvSetIndicesArray.getCount ();
        polyFace.mu = new MayaDM::polyFaces::MU [ numUVSets ];
        polyFace.muCount = numUVSets;
        for ( size_t i=0; i<numUVSets; ++i )
        {
            // Get the index of the uv set
            const COLLADAFW::IndexList* indexList = primitiveElement->getUVCoordIndices ( i );
            String uvSetName = indexList->getName ();
            size_t index = mesh->getUVSetIndexByName ( uvSetName );

            const COLLADAFW::MeshVertexData& meshUVCoords = mesh->getUVCoords ();
            polyFace.mu[i].uvSet = (int) index;
            polyFace.mu[i].faceUVCount = numEdges;
            polyFace.mu[i].uvIdValue = new int [numEdges];

            for ( int j=0; j<numEdges; ++j )
            {
                size_t currentIndexPosition = j + uvSetIndicesIndex;
                unsigned int currentIndex = indexList->getIndex ( currentIndexPosition );
                // Decrement the index values
                size_t initialIndex = indexList->getInitialIndex ();
                polyFace.mu[i].uvIdValue [j] = currentIndex - initialIndex;
            }
        }
        // Increment the current uv set index for the number of edges.
        uvSetIndicesIndex += numEdges;
    }

    // --------------------------------------------
    void GeometryImporter::setColorInfos ( 
        const COLLADAFW::Mesh* mesh, 
        const COLLADAFW::MeshPrimitive* primitiveElement, 
        MayaDM::polyFaces &polyFace, 
        size_t& colorIndicesIndex, 
        const int numEdges )
    {
        const COLLADAFW::IndexListArray& colorIndicesArray = primitiveElement->getColorIndicesArray ();
        size_t numColorInputs = colorIndicesArray.getCount ();
        polyFace.mc = new MayaDM::polyFaces::MC [ numColorInputs ];
        polyFace.mcCount = numColorInputs;
        for ( size_t i=0; i<numColorInputs; ++i )
        {
            // Get the index of the uv set
            const COLLADAFW::IndexList* indexList = primitiveElement->getColorIndices ( i );
            String colorInputName = indexList->getName ();
            size_t index = mesh->getColorIndexByName ( colorInputName );

            const COLLADAFW::MeshVertexData& meshColors = mesh->getColors ();
            polyFace.mc[i].colorSet = (int) index;
            polyFace.mc[i].faceColorCount = numEdges;
            polyFace.mc[i].colorIdValue = new int [numEdges];

            for ( int j=0; j<numEdges; ++j )
            {
                size_t currentIndexPosition = j + colorIndicesIndex;
                unsigned int currentIndex = indexList->getIndex ( currentIndexPosition );
                // Decrement the index values
                size_t initialIndex = indexList->getInitialIndex ();
                polyFace.mc[i].colorIdValue [j] = currentIndex - initialIndex;
            }
        }
        // Increment the current uv set index for the number of edges.
        colorIndicesIndex += numEdges;
    }

    // --------------------------------------------
    COLLADABU::Math::Vector3* GeometryImporter::getVertexPosition ( 
        const COLLADAFW::Mesh* mesh , 
        const size_t vertexIndex )
    {
        // Get the points of the current edge.
        const COLLADAFW::MeshVertexData& meshPositions = mesh->getPositions ();

        // Get the vertex position values of the start position of the current edge.
        double dx, dy, dz; 
        if ( meshPositions.getType () == COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT )
        {
            dx = (double)((*(meshPositions.getFloatValues ()))[(vertexIndex*3)]);
            dy = (double)((*(meshPositions.getFloatValues ()))[(vertexIndex*3)+1]);
            dz = (double)((*(meshPositions.getFloatValues ()))[(vertexIndex*3)+2]);
        }
        else if ( meshPositions.getType () == COLLADAFW::MeshVertexData::DATA_TYPE_DOUBLE )
        {
            dx = (*(meshPositions.getDoubleValues ()))[(vertexIndex*3)];
            dy = (*(meshPositions.getDoubleValues ()))[(vertexIndex*3)+1];
            dz = (*(meshPositions.getDoubleValues ()))[(vertexIndex*3)+2];
        }
        else
        {
            std::cerr << "GeometryImporter::appendPolyFaces:: Unknown data type!" << std::endl;
            MGlobal::displayError ( "GeometryImporter::appendPolyFaces:: Unknown data type!" );
            assert ( "GeometryImporter::appendPolyFaces:: Unknown data type!" );
        }

        // Store the vertex positions of the current start point.
        return new COLLADABU::Math::Vector3 ( dx, dy, dz );;
    }

    // --------------------------------------------
    bool GeometryImporter::changeHoleOrientation ( 
        std::vector<COLLADABU::Math::Vector3*>& polygonPoints, 
        std::vector<COLLADABU::Math::Vector3*>& holePoints )
    {
        // Flag, if the orientation of the hole element has to be changed.
        bool changeOrientation = false;

        // Get the cross product of the parenting polygon.
        COLLADABU::Math::Vector3 p1 = *polygonPoints [1] - *polygonPoints [0];
        COLLADABU::Math::Vector3 p2 = *polygonPoints [2] - *polygonPoints [0];
        COLLADABU::Math::Vector3 polyCrossProduct = p1.crossProduct ( p2 );

        // Get the cross product of the hole.
        COLLADABU::Math::Vector3 h1 = *holePoints [1] - *holePoints [0];
        COLLADABU::Math::Vector3 h2 = *holePoints [2] - *holePoints [0];
        COLLADABU::Math::Vector3 holeCrossProduct = h1.crossProduct ( h2 );

        // If they have the same orientation, we have to change the holes orientation.
        if ( polyCrossProduct.dotProduct ( holeCrossProduct ) > 0 )
        {
            // Change the hole's orientation.
            changeOrientation = true;
        }	
        
        return changeOrientation;
    }

    // --------------------------------------------
    void GeometryImporter::changePolyFaceHoleOrientation ( MayaDM::polyFaces &polyFace )
    {
        // Get the edge indices in the different order and change the orientation.
        std::vector<int> valueVec;

        // Edge count
        int numEdges = polyFace.h.holeEdgeCount;

        // Change the orientation and write it into the valueVec.
        for ( int edgeIndex=numEdges-1; edgeIndex>=0; --edgeIndex )
        {
            int edgeIndexValue = polyFace.h.edgeIdValue[edgeIndex];
            edgeIndexValue = ( edgeIndexValue + 1 ) * (-1);
            valueVec.push_back ( edgeIndexValue );
        }
        // Write the edge list index from the valueVec into polyFace.
        for ( size_t edgeIndex=0; edgeIndex<valueVec.size (); ++edgeIndex )
        {
            polyFace.h.edgeIdValue[edgeIndex] = valueVec[edgeIndex];
        }
    }

    // --------------------------------------------
    const MayaNode* GeometryImporter::findMayaMeshNode ( const COLLADAFW::UniqueId& uniqueId ) const
    {
        UniqueIdMayaNodesMap::const_iterator it = mMayaMeshNodesMap.find ( uniqueId );
        if ( it != mMayaMeshNodesMap.end () )
            return &(*it).second;

        return NULL;
    }

    // --------------------------------------------
    MayaNode* GeometryImporter::findMayaMeshNode ( const COLLADAFW::UniqueId& uniqueId )
    {
        UniqueIdMayaNodesMap::iterator it = mMayaMeshNodesMap.find ( uniqueId );
        if ( it != mMayaMeshNodesMap.end () )
            return &(*it).second;

        return NULL;
    }

    // --------------------------------------------
    const MayaDM::Mesh* GeometryImporter::findMayaDMMeshNode ( const COLLADAFW::UniqueId& uniqueId ) const
    {
        UniqueIdMayaDMMeshMap::const_iterator it = mMayaDMMeshNodesMap.find ( uniqueId );
        if ( it != mMayaDMMeshNodesMap.end () )
            return &(*it).second;

        return NULL;
    }

    // --------------------------------------------
    MayaDM::Mesh* GeometryImporter::findMayaDMMeshNode ( const COLLADAFW::UniqueId& uniqueId )
    {
        UniqueIdMayaDMMeshMap::iterator it = mMayaDMMeshNodesMap.find ( uniqueId );
        if ( it != mMayaDMMeshNodesMap.end () )
            return &(*it).second;

        return NULL;
    }

    // --------------------------------------------
    bool GeometryImporter::getEdgeIndex ( 
        const COLLADAFW::Edge& edge, 
        const std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap, 
        int& edgeIndex )
    {
        // Find the index of the edge in the map of edges to get the index in the list.
        std::map<COLLADAFW::Edge,size_t>::const_iterator it = edgeIndicesMap.find ( edge );
        if ( it == edgeIndicesMap.end() ) 
        {
            // The edge has to be in the map!!!
            MString message ( "Edge not found: " ); message += edge[0] + ", " + edge[1];
            MGlobal::displayError ( message );
            std::cerr << message.asChar () << std::endl;
            assert ( it != edgeIndicesMap.end() );
        }
        edgeIndex = (int)it->second; 
        if ( edge.isReverse() ) edgeIndex = -( edgeIndex + 1 );

        return true;
    }

    // --------------------------------------------
    void GeometryImporter::getEdgeIndices ( 
        const COLLADAFW::Mesh* mesh, 
        std::vector<COLLADAFW::Edge>& edgeIndices, 
        std::map<COLLADAFW::Edge,size_t>& edgeIndicesMap )
    {
        // Implementation for multiple primitive elements.
        const COLLADAFW::MeshPrimitiveArray& primitiveElementsArray = mesh->getMeshPrimitives ();
        size_t count = primitiveElementsArray.getCount ();
        for ( size_t i=0; i<count; ++i )
        {
            COLLADAFW::MeshPrimitive* primitiveElement = primitiveElementsArray [ i ];

            // Determine the edge indices (unique edges, also for multiple primitive elements).
            primitiveElement->appendEdgeIndices ( edgeIndices, edgeIndicesMap );
        }
    }

    // --------------------------------------------
    std::vector<size_t>* GeometryImporter::getShadingEnginePrimitiveIndices ( 
        const COLLADAFW::UniqueId& geometryId, 
        const COLLADAFW::MaterialId shadingEngineId )
    {
        CombinedId combinedId ( geometryId, shadingEngineId );
        CombinedIdIndicesMap::iterator it = mShadingEnginePrimitivesMap.find ( combinedId );
        if ( it == mShadingEnginePrimitivesMap.end () )
        {
            return 0;
        }
        return &it->second;
    }

    // --------------------------------------------
    void GeometryImporter::setShadingEnginePrimitiveIndex ( 
        const COLLADAFW::UniqueId& geometryId, 
        const COLLADAFW::MaterialId shadingEngineId, 
        const size_t primitiveIndex )
    {
        CombinedId combinedId ( geometryId, shadingEngineId );
        mShadingEnginePrimitivesMap [combinedId].push_back ( primitiveIndex );
    }
}