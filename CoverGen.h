// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Containers/Array.h"
#include "CoverTriggerBox.h"
/**
 * 
 */
class COVERSYSTEM_API CoverGen
{
public:
	CoverGen(UWorld* worldPtr);
	~CoverGen();

private:
	UWorld* _pWorld = nullptr;

	struct CoverActors
	{
		TArray<AActor*> DynamicActors;
		TArray<AActor*> StaticActors;
	};

	class CoverNode
	{

	friend CoverGen;

	public:
		CoverNode(int32 index, FVector position, FVector normal) : _iIndex(index), _VPosition(position), _VNormal(normal), _fHeight(position.Z) {};
		~CoverNode() {}

	private:
		int32	_iIndex    = -1;
		FVector _VPosition = { 0.0f, 0.0f ,0.0f };
		FVector _VNormal   = { 0.0f, 0.0f ,0.0f };
		float   _fHeight    = 0.0f;
		bool _bConnectedNode = false; // Node has connection to another node
		bool _bMainNode = false; //if the node is the first node we start optimization from (we can have multiple main nodes if there are holes in geometry)
		ACoverTriggerBox* _triggerBox = nullptr;

	public:
		inline FVector GetPosition() const { return _VPosition; }
		inline FVector GetNormal()   const { return _VNormal;   }
		inline float   GetHeight()   const { return _fHeight;   }
	};


	class CoverObject
	{
	friend CoverGen;

	public:
		 CoverObject() {}
		~CoverObject() {}

	private:
		int32 _ID = -1;
		FString _Name = "Unknown";
		TArray<CoverNode*> _coverNodes;
		FVector vLocation = { 0.0f, 0.0f, 0.0f };   //general location used to calculate a distance from another entity
		FVector _vScale   = { 0.0f, 0.0f, 0.0f };   //general scale of the object

	public:
		inline const FVector GetLocation()           { return vLocation;   }
		inline const FVector GetSize()               { return _vScale;       }
		inline TArray<CoverNode*> GetAllCoverNodes() { return _coverNodes; }
		inline FString GetName()                     { return _Name;       }
	private:
		inline void SetLocation(FVector Location)    { vLocation = Location; }
		inline void SetSize(FVector Size)            { _vScale = Size; }
		CoverNode* AddNewCoverPoint(FVector nodePosition, FVector nodeNormal);
		void CopyCoverNodes(TArray<CoverNode*> &copyFrom);
		void RemoveCoverNodes(TArray<CoverNode*>& nodesToBeRemoved);
		TArray<CoverNode*> GetTheLowestChainOfNodes(float spacing);
		void OrganizeNodeArrayByLocation();
	};

	struct CoverObjects
	{
		TArray<CoverObject*> DynamicCoverObjects;
		TArray<CoverObject*> StaticCoverObjects;
	};

protected:	
	//CoverActors* actorArray = nullptr;

private:
	CoverObjects* allCoverObjects = nullptr; //to store a list of static and dynamic cover objects

	//used to store two connected vertices that can be later used for a line trace
	struct Edge2
	{
		FVector vP1;
		FVector vP2;
		FVector vNormal;
		FVector vDirection;

		Edge2(FVector Ponit1, FVector Point2, FVector Normal, FVector Direction) :
			vP1(Ponit1),
			vP2(Point2),
			vNormal(Normal),
			vDirection(Direction)
		{;}
	};

private:
	void GenerateCoverPoints(int32 levelIndex = 0, float spacing = 10.0f);
	CoverActors* GetActorsWithCoverFlagInTheScene();
	FVector RayHitTest(FVector StartTrace, FVector ForwardVector, float MaxDistance, AActor* ActorTested, FVector &outNormal,  FColor rayDebugColor = FColor::Red);
	inline void DrawBoundingBoxEdges(AActor*& actorRef);
	inline bool isVecHeightInBounds(const float& boundingBoxBottom, FVector& vec, float min, float max);
	inline void MargeNodesInProximity(CoverObject*& coverObject, float radius, bool margeOnlyNodesWithTheSameNormal = true);
	inline void MargeNodesInProximity2D(CoverObject*& coverObject, float radius);
	inline void DebugDrawAllCoverNodes();
	inline void DebugCheckForDuplicates(CoverObject*& coverObject);
	inline void OrganizeCoverNodesByDistance(CoverObject*& _coverObject);
	inline void OptimizeCoverNodes(CoverObject*& _coverObject, float _spacing);
	inline void RemoveUpAndDownNodes(CoverObject*& _coverObject, float maxUp = 0.8f); // used to remove nodes that's normal faces too much up or down as these are not valid cover nodes
	inline float roundFloat(float& var) { float value = (int)(var * 100.0f + 0.5f); return (float)value / 100.0f; }
	inline FVector roundVector(FVector& vec) { return FVector(roundFloat(vec.X), roundFloat(vec.Y), roundFloat(vec.Z)); }
	inline void SortArrayByLowestHeight(TArray<FVector>& arr);
	inline bool NormalCheck2D(FVector& Normal, float Range) { if (Normal.X < Range && Normal.X > -Range && Normal.Y < Range && Normal.Y > -Range) return true; return false; }

	//Accessing geometry data
	inline TArray<FVector> GetActorsVertexPositon(AActor* actor);
	inline TArray<FVector> ReconstructAndScaleActorTriangles(AActor* actor);
	inline FVector CalculateSurfaceNormalOfATriangle(FVector& p1, FVector& p2, FVector& p3);
	inline FVector CalculateCenterOfATriangle(FVector& p1, FVector& p2, FVector& p3);
	//inline FVector CalculateAndCenterNormalOfATriangle(FVector& p1, FVector& p2, FVector& p3);
	inline bool isTriangleInZRange(float MinZ, float MaxZ, FVector& p1, FVector& p2, FVector& p3);
	inline void CreateEdgeLinks(const TArray<FVector>& triangles, TArray<FVector>& vertices, TArray<Edge2*>& edgesOut);
	inline void FindEdgeLink(int& currentIndex, TArray<FVector>& vertices, TArray<CoverGen::Edge2*>& edgesOut, FVector& V1, FVector& V2, FVector& V3, FVector triangleNormal, bool ignoreSurfacesWithVerticalFaces = false);

	//Trigger box generation
	inline void GetNodesInRadius(CoverObject*& _coverObject, FVector _searchPos, float _searchRadius, TArray<CoverNode*>& _outCoverNodesFound);
	inline CoverNode* GetLowestNodeInPosition(CoverObject*& _coverObject, FVector2D _searchPos, float _posErrorAcceptance);
	void CreateTriggerBoxData(CoverObject*& _coverObject);
	inline void SetTriggerBoxTransform(ACoverTriggerBox* triggerBox, CoverNode* currentNode, CoverNode* nextNode, const FVector objectsScale);
	//inline void CreateCoverNodesFromPositionVectors(TArray)
};
