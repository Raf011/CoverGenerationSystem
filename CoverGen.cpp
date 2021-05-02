// Fill out your copyright notice in the Description page of Project Settings.


#include "CoverGen.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "DrawDebugHelpers.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Templates/UniquePtr.h"

#include "PhysicsPublic.h"
#include "PhysXIncludes.h"
#include "PhysXPublic.h"
#include "Engine/TriggerBox.h"
//#include "ThirdParty/PhysX/PhysX-3.3/include/geometry/PxTriangleMesh.h"
//#include "ThirdParty/PhysX/PhysX-3.3/include/foundation/PxSimpleTypes.h"

// 0 - OFF, 1 - Draw Cover Nodes, 2 - Draw Cover Nodes & Reconstructed Triangles, 3 - Draw Reconstructed Triangles
#define VisualDebug 2
#define DebugMode
//  0 - OFF, 1 - Draw front, 2 - Draw left, 3 - Draw back, 4 - Draw right, 5 - Draw all
#define DrawMissedRays 0

//TODO: Turn into a singleton
CoverGen::CoverGen(UWorld* worldPtr) : _pWorld(worldPtr)
{
	//GetActorsWithCoverFlagInTheScene();
	GenerateCoverPoints(0, 20.0f);
}

CoverGen::~CoverGen()
{
}

void CoverGen::GenerateCoverPoints(int32 levelIndex, float spacing)
{
	if (_pWorld)
	{
		ULevel* level = _pWorld->GetLevel(levelIndex);
		TArray<AActor*> allActors = level->Actors;
		allCoverObjects = new CoverObjects();

		for (AActor* actor : allActors)
		{
			if (actor->ActorHasTag("NoCover"))
				continue;

			const float testAboveZ = 226.0f; //226.0f; ??????????????????????
			const float   fTopOfTheBoundingBox = actor->GetComponentsBoundingBox().GetCenter().Z + actor->GetComponentsBoundingBox().GetSize().Z / 2.0f;

			if(actor->GetActorEnableCollision() && fTopOfTheBoundingBox >= testAboveZ)
			{
				CoverObject* ptrCurrentCoverObject = new CoverObject();
				ptrCurrentCoverObject->SetLocation(actor->GetComponentsBoundingBox().GetCenter());//set location
				ptrCurrentCoverObject->SetSize(actor->GetComponentsBoundingBox().GetSize());//set size
				ptrCurrentCoverObject->_Name = actor->GetName();//set name
				ptrCurrentCoverObject->_ID = allCoverObjects->DynamicCoverObjects.Num() + allCoverObjects->StaticCoverObjects.Num();
				ptrCurrentCoverObject->_vScale = actor->GetActorScale();

				const float minCover = 50.0f;
				const float maxCover = 180.0f;
				const float groundLevel = 130.0f;

				const float SmallOffset = 1.0f;
				const float LargeOffset = 100.0f; // how far away from the bounding box/geometry we want to shoot the ray from (lowering it can help with narrow spaces)

				const int missAcceptance = 2;
				const float maxDistance = LargeOffset + spacing + 200.0f;

				const FVector boundingBoxCenter = actor->GetComponentsBoundingBox().GetCenter();
				const FVector sizeHalfed = actor->GetComponentsBoundingBox().GetSize() / 2.0f;
				bool objectClipsThroughGorund = (boundingBoxCenter.Z - sizeHalfed.Z) < groundLevel;
				const float   fBottomOfTheBoundingBox = objectClipsThroughGorund ? groundLevel : boundingBoxCenter.Z - sizeHalfed.Z; // to calculate how far up we can go
				
				// is cover static or dynamic?
				// Dynamic cover
				if (actor->IsRootComponentMovable())
					allCoverObjects->DynamicCoverObjects.Add(ptrCurrentCoverObject);

				// Static cover
				else
					allCoverObjects->StaticCoverObjects.Add(ptrCurrentCoverObject);


				//Start cover generation:
				//OPTION 1 -  use object's geometry for cover generation
				if (actor->ActorHasTag("CoverFromGeometry"))
				{
					//DEBUG DRAW SPHERE OVER "CoverFromGeometry" OBJECT
					FVector DebugSpherePos = actor->GetComponentsBoundingBox().GetCenter();
					DebugSpherePos.Z += actor->GetComponentsBoundingBox().GetSize().Z / 2.0f + 50.0f;
					DrawDebugSphere(_pWorld, DebugSpherePos, 10.0f, 2, FColor::White, true);


					//##### 1. Retrieve geometry data #####//

					//# 1a. Retrieve triangles #//
					const TArray<FVector> scaledTris = ReconstructAndScaleActorTriangles(actor);

					//# 1b. Retrieve vertices #//
					TArray<FVector> allVerts = GetActorsVertexPositon(actor);
					TArray<FVector> verts;

					//filter vertices in cover range, minCoverHeight - maxCoverHeight
					for (FVector vert : allVerts)
					{
						if (vert.Z < fBottomOfTheBoundingBox + maxCover)
						{
							verts.Add(vert);
						}
					}

					//sort vertices by height < 
					SortArrayByLowestHeight(verts);

					//delete vertices on the same X and Y axis as we only need one (lowest of each)
					TArray<FVector> deleteVerts;
					for (int vertIndex = 0; vertIndex < verts.Num() - 1; ++vertIndex)
					{
						for (int vertIndex2 = vertIndex + 1; vertIndex2 < verts.Num(); ++vertIndex2)
						{
							int X1 = (int)verts[vertIndex].X;
							int X2 = (int)verts[vertIndex2].X;
							int Y1 = (int)verts[vertIndex].Y;
							int Y2 = (int)verts[vertIndex2].Y;

							if (X1 == X2 && Y1 == Y2)
								deleteVerts.Add(verts[vertIndex2]);
						}
					}

					//store only filtered/valid vertices
					allVerts.Empty();

					for (FVector vert : verts)
						if (!(deleteVerts.Contains(vert)))
							allVerts.Add(vert);

					deleteVerts.Empty();
					verts.Empty();

					//create edge links using our filtered vertices 
					TArray<Edge2*> edgeLinks;
					CreateEdgeLinks(scaledTris, allVerts, edgeLinks);

					//ray trace using edge links
					for (auto eLink : edgeLinks)
					{

						float arrowLen = FVector::Distance(eLink->vP1, eLink->vP2) / 2.0f;
						FVector middlePoint = eLink->vP1 + eLink->vDirection * arrowLen;
						
						eLink->vNormal = -(eLink->vNormal); //reverse normal
						//if object's scale is negative reverse the normal on equivalent axis
						if (actor->GetActorScale().X < 0.0f)  eLink->vNormal = -eLink->vNormal;
						if (actor->GetActorScale().Y < 0.0f)  eLink->vNormal = -eLink->vNormal;
						if (actor->GetActorScale().Z < 0.0f)  eLink->vNormal = -eLink->vNormal;

						DrawDebugDirectionalArrow(_pWorld, eLink->vP1, middlePoint, 2.0f, FColor::Red, true);
						DrawDebugDirectionalArrow(_pWorld, middlePoint, middlePoint + eLink->vNormal * 5.0f, 5.0f, FColor::Yellow, true);
					
						//Add a small offset to avoid clipping
						eLink->vP1 += eLink->vDirection * 2.0f;
						eLink->vP2 -= eLink->vDirection * 2.0f;

						if (FVector::Distance(eLink->vP1, eLink->vP2) > spacing * 2.0f)
						{
							int maxRayCount = int(FVector::Distance(eLink->vP1, eLink->vP2) / spacing);
							FVector vNormal; // vector to store our normal

							for (int offset = 0; offset <= maxRayCount; ++offset)
							{
								int currentMissCount = 0;
								CoverNode* currentCoverNode = nullptr;
								float currentSpacing = (float)(offset * spacing);
								float fBottomPoint = fBottomOfTheBoundingBox;

								for (float heightOffset = minCover; fBottomPoint + heightOffset <= fTopOfTheBoundingBox; heightOffset += spacing)
								{
									float currentHeight = fBottomPoint + heightOffset;
									FVector pos = FVector(eLink->vP1.X + eLink->vDirection.X * currentSpacing, eLink->vP1.Y + eLink->vDirection.Y * currentSpacing, currentHeight);
									pos += eLink->vNormal * LargeOffset;

									if (isVecHeightInBounds(fBottomPoint, pos, minCover, maxCover) && currentMissCount <= missAcceptance)
									{
										float maxRayDistance = currentCoverNode ? FVector::Distance(currentCoverNode->GetPosition(), pos) + spacing : maxDistance;

										//maxRayDistance
										FVector hitRes = RayHitTest(pos, -eLink->vNormal, maxRayDistance, actor, vNormal);

										if (hitRes != FVector::ZeroVector)
										{
											//only create the first cover node
											if (!currentCoverNode)
												currentCoverNode = ptrCurrentCoverObject->AddNewCoverPoint(hitRes, vNormal);

											else
												currentCoverNode->_fHeight = hitRes.Z;

											currentMissCount = 0;
										}

										else
										{
											currentMissCount++;
										}
									}
									else break;
								}
							}
						}

						//if distance between two points is < spacing * 2.0f, start ray trace between two points and move up (don't move to the sides)
						else
						{
							int currentMissCount = 0;
							CoverNode* currentCoverNode = nullptr;
							float fBottomPoint = fBottomOfTheBoundingBox;
							FVector vNormal; // vector to store our normal

							for (float heightOffset = minCover; fBottomPoint + heightOffset <= fTopOfTheBoundingBox; heightOffset += spacing)
							{
								float currentHeight = fBottomPoint + heightOffset;
								FVector pos = FVector(middlePoint.X, middlePoint.Y, currentHeight);
								pos += eLink->vNormal * LargeOffset;

								if (isVecHeightInBounds(fBottomPoint, pos, minCover, maxCover) && currentMissCount <= missAcceptance)
								{
									float maxRayDistance = currentCoverNode ? FVector::Distance(currentCoverNode->GetPosition(), pos) + spacing : maxDistance;
									
									FVector hitRes = RayHitTest(pos, -eLink->vNormal, maxRayDistance, actor, vNormal);

									if (hitRes != FVector::ZeroVector)
									{
										//only create the first cover node
										if (!currentCoverNode)
											currentCoverNode = ptrCurrentCoverObject->AddNewCoverPoint(hitRes, vNormal);

										else
											currentCoverNode->_fHeight = hitRes.Z;

										currentMissCount = 0;
									}

									else
									{
										currentMissCount++;
									}
								}
								else break;
							}
						}
					}

					//clear edge links as we don't need them anymore
					for (auto edgeLink : edgeLinks)
						delete edgeLink;

					//############################ END cover from geometry ############################//

					MargeNodesInProximity(ptrCurrentCoverObject, spacing / 2.0f, true);
				}

				// OPTION 2 - use bounding box for cover generation (simple)
				else
				{
					//TODO: fix the min bounding box
					const FVector leftFront = FVector((boundingBoxCenter.X - sizeHalfed.X), boundingBoxCenter.Y - sizeHalfed.Y, boundingBoxCenter.Z);
					const FVector rightFront = FVector((boundingBoxCenter.X - sizeHalfed.X), boundingBoxCenter.Y + sizeHalfed.Y, boundingBoxCenter.Z);
					const FVector leftBack = FVector((boundingBoxCenter.X + sizeHalfed.X), boundingBoxCenter.Y - sizeHalfed.Y, boundingBoxCenter.Z);
					const FVector rightBack = FVector((boundingBoxCenter.X + sizeHalfed.X), boundingBoxCenter.Y + sizeHalfed.Y, boundingBoxCenter.Z);
					
					//shoot at different heights
					if(fTopOfTheBoundingBox < 50000.0f)
					{
						FVector vNormal; // vector to store our normal

						//shoot multiple rays from 4 directions:
						//############ on Y axis front ############//
						for (float offset = 0.0f; leftFront.Y + offset <= rightFront.Y; offset += spacing)
						{
							int currentMissCount = 0;
							CoverNode* currentCoverNode = nullptr;

							for (float heightOffset = minCover; fBottomOfTheBoundingBox + heightOffset <= fTopOfTheBoundingBox; heightOffset += spacing)
							{
								float currentHeight = fBottomOfTheBoundingBox + heightOffset;
								FVector pos = FVector(leftFront.X - LargeOffset, leftFront.Y + offset, currentHeight);
								
								if (isVecHeightInBounds(fBottomOfTheBoundingBox, pos, minCover, maxCover) && currentMissCount <= missAcceptance)
								{
									float maxRayDistance = currentCoverNode ? FVector::Distance(currentCoverNode->GetPosition(), pos) + spacing : maxDistance;

									FVector hitRes = RayHitTest(pos, pos.ForwardVector, maxRayDistance, actor, vNormal);

									if (hitRes != FVector::ZeroVector)
									{
										//only create the first cover node
										if(!currentCoverNode)
											currentCoverNode = ptrCurrentCoverObject->AddNewCoverPoint(hitRes, vNormal);

										else
											currentCoverNode->_fHeight = hitRes.Z;

#if DrawMissedRays == 1 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Green, true);
#endif
										currentMissCount = 0;
									}

									else
									{
										currentMissCount++;
#if DrawMissedRays == 1 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Red, true);
										DrawDebugLine(_pWorld, pos, FVector(pos.X + 1.0f + maxRayDistance, pos.Y, pos.Z), FColor::Red, true);
#endif
									}
								}
								else break;
							}
						}

						//############ on X axis right ############//
						for (float offset = 0.0f; rightFront.X + offset <= rightBack.X; offset += spacing)
						{
							int currentMissCount = 0;
							CoverNode* currentCoverNode = nullptr;

							for (float heightOffset = minCover; fBottomOfTheBoundingBox + heightOffset <= fTopOfTheBoundingBox; heightOffset += spacing)
							{
								float currentHeight = fBottomOfTheBoundingBox + heightOffset;
								FVector pos = FVector(rightFront.X + offset, rightFront.Y + LargeOffset, currentHeight);

								if (isVecHeightInBounds(fBottomOfTheBoundingBox, pos, minCover, maxCover) && currentMissCount <= missAcceptance)
								{
									float maxRayDistance = currentCoverNode ? FVector::Distance(currentCoverNode->GetPosition(), pos) + spacing : maxDistance;

									FVector hitRes = RayHitTest(pos, pos.LeftVector, maxRayDistance, actor, vNormal);
									

									if (hitRes != FVector::ZeroVector)
									{
										//only create the first cover node
										if (!currentCoverNode)
											currentCoverNode = ptrCurrentCoverObject->AddNewCoverPoint(hitRes, vNormal);

										else
											currentCoverNode->_fHeight = hitRes.Z;

#if DrawMissedRays == 4 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Green, true);
#endif
										currentMissCount = 0;
									}

									else
									{
										currentMissCount++;
#if DrawMissedRays == 4 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Red, true);
										DrawDebugLine(_pWorld, pos, FVector(pos.X, pos.Y - 1.0f - maxRayDistance, pos.Z), FColor::Red, true);
#endif
									}
								}
								else break;
							}
						}

						//############ on Y axis back ############//
						for (float offset = 0; leftBack.Y <= rightBack.Y - offset; offset += spacing)
						{
							int currentMissCount = 0;
							CoverNode* currentCoverNode = nullptr;

							for (float heightOffset = minCover; fBottomOfTheBoundingBox + heightOffset <= fTopOfTheBoundingBox; heightOffset += spacing)
							{
								float currentHeight = fBottomOfTheBoundingBox + heightOffset;
								FVector pos = FVector(rightBack.X + LargeOffset, rightBack.Y - offset, currentHeight);

								if (isVecHeightInBounds(fBottomOfTheBoundingBox, pos, minCover, maxCover) && currentMissCount <= missAcceptance)
								{
									float maxRayDistance = currentCoverNode ? FVector::Distance(currentCoverNode->GetPosition(), pos) + spacing : maxDistance;

									FVector hitRes = RayHitTest(pos, pos.BackwardVector, maxRayDistance, actor, vNormal);
									//DrawDebugSphere(_pWorld, pos, 6.5f, 2, FColor::Orange, true);

									if (hitRes != FVector::ZeroVector)
									{
										//only create the first cover node
										if (!currentCoverNode)
											currentCoverNode = ptrCurrentCoverObject->AddNewCoverPoint(hitRes, vNormal);

										else
											currentCoverNode->_fHeight = hitRes.Z;

#if DrawMissedRays == 3 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Green, true);
#endif
										currentMissCount = 0;
									}

									else
									{
										currentMissCount++;
#if DrawMissedRays == 3 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Red, true);
										DrawDebugLine(_pWorld, pos, FVector(pos.X - 1.0f - maxRayDistance, pos.Y, pos.Z), FColor::Red, true);
#endif
									}
								}
								else break;
							}
						}
						//_________ Y axis end _________//

						//############ on X axis left ############//
						for (float offset = 0; leftFront.X <= leftBack.X - offset; offset += spacing)
						{
							int currentMissCount = 0;
							CoverNode* currentCoverNode = nullptr;

							for (float heightOffset = minCover; fBottomOfTheBoundingBox + heightOffset <= fTopOfTheBoundingBox; heightOffset += spacing)
							{
								float currentHeight = fBottomOfTheBoundingBox + heightOffset;
								FVector pos = FVector(leftBack.X - offset, leftBack.Y - LargeOffset, currentHeight);

								if (isVecHeightInBounds(fBottomOfTheBoundingBox, pos, minCover, maxCover) && currentMissCount <= missAcceptance)
								{
									float maxRayDistance = currentCoverNode ? FVector::Distance(currentCoverNode->GetPosition(), pos) + spacing : maxDistance;

									FVector hitRes = RayHitTest(pos, pos.RightVector, maxRayDistance, actor, vNormal);
									//DrawDebugSphere(_pWorld, pos, 6.5f, 2, FColor::Magenta, true);

									if (hitRes != FVector::ZeroVector)
									{
										//only create the first cover node
										if (!currentCoverNode)
											currentCoverNode = ptrCurrentCoverObject->AddNewCoverPoint(hitRes, vNormal);

										else
											currentCoverNode->_fHeight = hitRes.Z;

#if DrawMissedRays == 2 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Green, true);
#endif
										currentMissCount = 0;
									}

									else
									{
										currentMissCount++;
#if DrawMissedRays == 2 || DrawMissedRays == 5
										DrawDebugSphere(_pWorld, pos, 1.5f, 2, FColor::Red, true);
										DrawDebugLine(_pWorld, pos, FVector(pos.X, pos.Y + 1.0f + maxRayDistance, pos.Z), FColor::Red, true);
#endif
									}
								}
								else break;
							}
						}

					}
					MargeNodesInProximity(ptrCurrentCoverObject, spacing - 1.0f, false);
				}

				//Optimize cover
				RemoveUpAndDownNodes(ptrCurrentCoverObject, 0.9f);

				//if(actor->ActorHasTag("TEST"))
				if(ptrCurrentCoverObject->GetAllCoverNodes().Num() > 5 && !(actor->ActorHasTag("NoCoverOptimization")))
				{
					if(actor->ActorHasTag("CoverFromGeometry"))
						OrganizeCoverNodesByDistance(ptrCurrentCoverObject);

					OptimizeCoverNodes(ptrCurrentCoverObject, spacing);
				}

				//Set proper height value
				for (auto node : ptrCurrentCoverObject->GetAllCoverNodes())
					node->_fHeight = node->_fHeight - node->GetPosition().Z;

				if (actor->ActorHasTag("TEST2_"))
					CreateTriggerBoxData(ptrCurrentCoverObject);
			}
		}

#if VisualDebug > 0 && VisualDebug < 3
		DebugDrawAllCoverNodes();
#endif
	}
}

CoverGen::CoverActors* CoverGen::GetActorsWithCoverFlagInTheScene()
{
	CoverActors* actorArray = nullptr;

	if(_pWorld)
	{
		ULevel* level = _pWorld->GetLevel(0); // index 0 ?
		TArray<AActor*> allActors = level->Actors;
		actorArray = new CoverActors();

		//actor->ActorHasTag("Cover")
		for (AActor* actor : allActors)
		{

			//try to find a collision component
			const UBoxComponent* CollisionComp = actor->FindComponentByClass<UBoxComponent>();

			//##################
			UMeshComponent* CollisionMeshComp = actor->FindComponentByClass<UMeshComponent>();
			
			if(CollisionMeshComp)
			{
				//const FCollisionShape CollisionShape = CollisionMeshComp->GetCollisionShape();
				const UBodySetup* BodySetup = CollisionMeshComp->GetBodySetup();
				const ECollisionTraceFlag BodyFlag = BodySetup->CollisionTraceFlag;

				//Complex Collision Check
				if(BodyFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple)
				{
					//USES COMPLEX COLLISION
				}
			}
			//#################

			// does the actor contain collision component?
			if(CollisionComp)
			{
				if(CollisionComp->ComponentHasTag("CoverCollision"))
				{
					// is this a static object?
					if(actor->IsRootComponentStatic())
					{
						actorArray->StaticActors.Add(actor);
					}

					// is this a dynamic object?
					else if(actor->IsRootComponentMovable())
					{
						actorArray->DynamicActors.Add(actor);
					}
				}
			}
		}
	}
	
	return actorArray;
}

FVector CoverGen::RayHitTest(FVector StartTrace, FVector ForwardVector, float MaxDistance, AActor* ActorTested, FVector& outNormal, FColor rayDebugColor)
{
	FHitResult* HitResult = new FHitResult();
	FVector EndTrace = ForwardVector * MaxDistance + StartTrace;
	FCollisionQueryParams* TraceParams = new FCollisionQueryParams();

	if (_pWorld->LineTraceSingleByChannel(*HitResult, StartTrace, EndTrace, ECC_Visibility, *TraceParams))
	{
		if(HitResult->Actor.Get()->GetUniqueID() == ActorTested->GetUniqueID())
		{
#if VisualDebug > 0
				//DrawDebugLine    (_pWorld, StartTrace, EndTrace, FColor(255, 0, 0), true);
			//DrawDebugLine(_pWorld, StartTrace, HitResult->ImpactPoint, rayDebugColor, true);
			//DrawDebugSphere  (_pWorld, StartTrace, 2.5f, 5, FColor::Blue, true);
			//DrawDebugSphere  (_pWorld, HitResult->ImpactPoint, 2.5f, 5, FColor::Green, true); // hit result
				//DrawDebugString(_pWorld, HitResult->ImpactPoint, (TEXT("Actor Hit: %s"), HitResult->Actor->GetName()));
#endif // VisualDebug

			outNormal = HitResult->Normal;
			return HitResult->ImpactPoint;
		}
	}


	delete HitResult;
	delete TraceParams;
	return FVector(0.0f, 0.0f, 0.0f);
}

inline void CoverGen::DrawBoundingBoxEdges(AActor*& actorRef)
{
	//		Z
	//		|
	//		|
	//		| X
	//		|/________ Y

	//CALCULATE FACING X
#if VisualDebug == 4
	const FVector center = actorRef->GetComponentsBoundingBox().GetCenter();
	const FVector sizeHalfed   = actorRef->GetComponentsBoundingBox().GetSize() / 2.0f;

	const FVector leftFront  = FVector((center.X - sizeHalfed.X), center.Y - sizeHalfed.Y, center.Z);
	const FVector rightFront = FVector((center.X - sizeHalfed.X), center.Y + sizeHalfed.Y, center.Z);
	const FVector leftBack   = FVector((center.X + sizeHalfed.X), center.Y - sizeHalfed.Y, center.Z);
	const FVector rightBack  = FVector((center.X + sizeHalfed.X), center.Y + sizeHalfed.Y, center.Z);

	DrawDebugSphere(_pWorld, leftFront,  2.5f, 5, FColor::Purple, true);
	DrawDebugSphere(_pWorld, rightFront, 2.5f, 5, FColor::Purple, true);
	DrawDebugSphere(_pWorld, leftBack,   2.5f, 5, FColor::Purple, true);
	DrawDebugSphere(_pWorld, rightBack,  2.5f, 5, FColor::Purple, true);

	DrawDebugLine(_pWorld, leftFront, rightFront, FColor::Purple, true);
	DrawDebugLine(_pWorld, leftFront, leftBack,   FColor::Purple, true);
	DrawDebugLine(_pWorld, leftBack, rightBack,   FColor::Purple, true);
	DrawDebugLine(_pWorld, rightFront, rightBack, FColor::Purple, true);

	//DrawDebugString( _pWorld, leftFront,  TEXT("Left Front")  );
	//DrawDebugString( _pWorld, rightFront, TEXT("Right Front") );
	//DrawDebugString( _pWorld, leftBack,   TEXT("Left Back")   );
	//DrawDebugString( _pWorld, rightBack,  TEXT("Right Back")  );
#endif // !VisualDebug
}

inline void CoverGen::DebugCheckForDuplicates(CoverObject*& coverObject)
{
	TArray<CoverNode*> duplicates;
	for (int indexCurrentNode = 0; indexCurrentNode < coverObject->GetAllCoverNodes().Num(); ++indexCurrentNode)
	{
		CoverNode* cCurrentNode = coverObject->GetAllCoverNodes()[indexCurrentNode];

		for (int indexTestedNode = indexCurrentNode + 1; indexTestedNode < coverObject->GetAllCoverNodes().Num(); ++indexTestedNode)
		{
			CoverNode* cTestedNode = coverObject->GetAllCoverNodes()[indexTestedNode];

			if (cCurrentNode == cTestedNode)
				duplicates.Add(cTestedNode);
		}
	}

	ensureMsgf(duplicates.Num() == 0, TEXT("There was %i node duplicates"), duplicates.Num());
}

inline void CoverGen::RemoveUpAndDownNodes(CoverObject*& _coverObject, float maxUp)
{
	TArray<CoverNode*> nodesToBeDelted;

	for (auto node : _coverObject->GetAllCoverNodes())
		if (node->GetNormal().Z > maxUp || node->GetNormal().Z < -maxUp)
			nodesToBeDelted.Add(node);

	if(nodesToBeDelted.Num() > 0)
		_coverObject->RemoveCoverNodes(nodesToBeDelted);
}

inline void CoverGen::GetNodesInRadius(CoverObject*& _coverObject, FVector _searchPos, float _searchRadius, TArray<CoverNode*>& _outCoverNodesFound)
{
	for(CoverNode* coverNode : _coverObject->GetAllCoverNodes())
	{
		float distance = FVector::Distance(_searchPos, coverNode->GetPosition());
		if(distance <= _searchRadius)
		{
			_outCoverNodesFound.Add(coverNode);
		}
	}
}

inline FVector CoverGen::CalculateSurfaceNormalOfATriangle(FVector& p1, FVector& p2, FVector& p3)
{
	FVector U = p2 - p1;
	FVector V = p3 - p1;
	FVector Normal;

	//Using cross product formula
	Normal.X = U.Y * V.Z - U.Z * V.Y;
	Normal.Y = U.Z * V.X - U.X * V.Z;
	Normal.Z = U.X * V.Y - U.Y * V.X;

	return Normal;
}

inline FVector CoverGen::CalculateCenterOfATriangle(FVector& p1, FVector& p2, FVector& p3)
{
	FVector Center;

	Center.X = (p1.X + p2.X + p3.X) / 3;
	Center.Y = (p1.Y + p2.Y + p3.Y) / 3;
	Center.Z = (p1.Z + p2.Z + p3.Z) / 3;

	return Center;

}

inline void CoverGen::SortArrayByLowestHeight(TArray<FVector>& arr)
{
	for(int index1 = 0; index1 < arr.Num() - 1; ++index1)
	{
		for (int index2 = index1 + 1; index2 < arr.Num(); ++index2)
		{
			if(arr[index1].Z > arr[index2].Z)
			{
				arr.Swap(index1, index2);
			}
		}
	}
}

inline TArray<FVector> CoverGen::GetActorsVertexPositon(AActor* actor)
{
	TArray<FVector> VArr;

	//if (actor->ActorHasTag("TEST"))
	{
		UStaticMeshComponent* tempMesh = actor->FindComponentByClass<UStaticMeshComponent>();

		if (tempMesh)
		{
			UStaticMesh* ptrUSM = tempMesh->GetStaticMesh();

			if (ptrUSM)
			{
				if (ptrUSM->RenderData->LODResources.Num() > 0)
				{
					FPositionVertexBuffer* VertexBuffer = &(ptrUSM->RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer);

					if (VertexBuffer)
					{
						const int32 VertexCount = VertexBuffer->GetNumVertices();
						//DrawDebugString(_pWorld, actor->GetActorLocation(), TEXT("VertexCount = ") + FString::FromInt(VertexCount));
						for (int32 Index = 0; Index < VertexCount; Index++)
						{
							//This is in the Static Mesh Actor Class, so it is location and transform of the SMActor
							const FVector WorldSpaceVertexLocation = actor->GetActorLocation() + actor->GetTransform().TransformVector(VertexBuffer->VertexPosition(Index));
						
							//add to output FVector array

							//Unique check
							bool isUnique = true;
							for (FVector vec : VArr)
							{
								if (FVector::Distance(WorldSpaceVertexLocation, vec) < 1.0f)
								{
									isUnique = false;
									break;
								}
							}
							if (isUnique)
								VArr.Add(WorldSpaceVertexLocation);
						}

						//for (int32 Index = 0; Index < VArr.Num(); Index++)
						//{
						//	DrawDebugSphere(_pWorld, VArr[Index], 5.0f, 5, FColor::Yellow, true);
						//
						//	if (Index != 0)
						//	{
						//		DrawDebugLine(_pWorld, VArr[Index], VArr[Index - 1], FColor::Yellow, true);
						//	}
						//}
						//
						//DrawDebugString(_pWorld, FVector(actor->GetActorLocation().X, actor->GetActorLocation().Y, actor->GetActorLocation().Z + 20.0f), TEXT("Vector Array = ") + FString::FromInt(VArr.Num()));

					}
				}
			}
		}
	}

	return VArr;
}

inline bool CoverGen::isTriangleInZRange(float MinZ, float MaxZ, FVector& p1, FVector& p2, FVector& p3)
{
	if (p1.Z > MinZ || p2.Z > MinZ || p3.Z > MinZ)
		if (p1.Z < MaxZ || p2.Z < MaxZ || p3.Z < MaxZ)
			return true;

	return false;
}

inline TArray<FVector> CoverGen::ReconstructAndScaleActorTriangles(AActor* actor)
{
	TArray<FVector> VArr; //to store the coords
	UStaticMeshComponent* tempMesh = actor->FindComponentByClass<UStaticMeshComponent>();
	auto TempTriMesh = tempMesh->BodyInstance.BodySetup.Get()->TriMeshes[0];
	int32 I0, I1, I2;

	check(TempTriMesh);
	int32 TriNumber = TempTriMesh->getNbTriangles();
	const PxVec3* PVertices = TempTriMesh->getVertices();
	const void* Triangles = TempTriMesh->getTriangles();   // Grab triangle indices

	for (int32 TriIndex = 0; TriIndex < TriNumber; ++TriIndex)
	{
		if (TempTriMesh->getTriangleMeshFlags() & PxTriangleMeshFlag::e16_BIT_INDICES)
		{
			PxU16* P16BitIndices = (PxU16*)Triangles; I0 = P16BitIndices[(TriIndex * 3) + 0]; I1 = P16BitIndices[(TriIndex * 3) + 1]; I2 = P16BitIndices[(TriIndex * 3) + 2];
		}

		else
		{
			PxU32* P32BitIndices = (PxU32*)Triangles; I0 = P32BitIndices[(TriIndex * 3) + 0]; I1 = P32BitIndices[(TriIndex * 3) + 1]; I2 = P32BitIndices[(TriIndex * 3) + 2];
		}

		// Local position (unscaled)
		const FVector V0 = P2UVector(PVertices[I0]);
		const FVector V1 = P2UVector(PVertices[I1]);
		const FVector V2 = P2UVector(PVertices[I2]);

		const FVector ActorPos = actor->GetActorLocation();

		//Add scaled coords to our array
		VArr.Add(ActorPos + actor->GetTransform().TransformVector(V0));
		VArr.Add(ActorPos + actor->GetTransform().TransformVector(V1));
		VArr.Add(ActorPos + actor->GetTransform().TransformVector(V2));
	}

#if VisualDebug == 2 ||  VisualDebug == 3
	//Debug draw our triangles
	//for (int V = 2; V < VArr.Num(); V += 3)
	//{
	//	FVector V0 = VArr[V - 0];
	//	FVector V1 = VArr[V - 1];
	//	FVector V2 = VArr[V - 2];
	//
	//
	//	DrawDebugLine(_pWorld, V0, V1, FColor::Red, true);
	//	DrawDebugLine(_pWorld, V0, V2, FColor::Red, true);
	//	DrawDebugLine(_pWorld, V1, V2, FColor::Red, true);
	//}
#endif

	return VArr;
}

inline CoverGen::CoverNode* CoverGen::GetLowestNodeInPosition(CoverObject*& _coverObject, FVector2D _searchPos, float _posErrorAcceptance)
{
	const float zStart = _coverObject->GetLocation().Z - _coverObject->GetSize().Z / 2.0f;
	const float zEnd   = _coverObject->GetLocation().Z + _coverObject->GetSize().Z / 2.0f;
	
	for(float Z = zStart; Z < zEnd; ++Z)
	{
		for(CoverNode* coverNode : _coverObject->GetAllCoverNodes())
		{
			if(coverNode->GetPosition().Z == Z)
			{
				float distance = FVector2D::Distance(FVector2D(coverNode->GetPosition().X, coverNode->GetPosition().Y), _searchPos);

				if(distance <= _posErrorAcceptance)
				{
					return coverNode;
				}
			}
		}
	}

	return nullptr;
}

void CoverGen::CreateTriggerBoxData(CoverObject*& _coverObject)
{
	int index = 0;
	for(auto node : _coverObject->GetAllCoverNodes())
	{
		if(node->_bConnectedNode)
		{
			//auto node = _coverObject->GetAllCoverNodes()[0];

			const FRotator* rot = new FRotator(0.0f, 0.0f, 0.0f);//create temp rotator
			AActor* tBoxA = _pWorld->SpawnActor(ACoverTriggerBox::StaticClass(), &node->_VPosition, rot); //spawn trigger box in the world
			delete rot; //delete temp rotator

			//tBoxA->SetActorScale3D(FVector(0.1f, 0.1f, 0.1f));
			//tBoxA->SetActorRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
			// 
			//ACoverTriggerBox* triggerBox = dynamic_cast<ACoverTriggerBox*>(tBoxA); // also works but will use UE4 version ACoverTriggerBox* triggerBox = Cast<ACoverTriggerBox>(tBoxA)

			if (ACoverTriggerBox* triggerBox = Cast<ACoverTriggerBox>(tBoxA))
			{
				node->_triggerBox = triggerBox;
				
				if(index < _coverObject->GetAllCoverNodes().Num() - 1)
					SetTriggerBoxTransform(triggerBox, node, _coverObject->GetAllCoverNodes()[index + 1], _coverObject->_vScale);

				triggerBox->DebugDrawTriggerBox();
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("dynamic cast to ACoverTriggerBox was unsuccessful. Object: %s, NodeID: %d"), *_coverObject->_Name, node->_iIndex);
				delete tBoxA; // we can't convert back to ACoverTriggerBox so we don't need this object anymore.
			}

			index++;
		}
	}
}

inline void CoverGen::SetTriggerBoxTransform(ACoverTriggerBox* triggerBox, CoverNode* currentNode, CoverNode* nextNode, const FVector objectsScale)
{
	const float triggerBoxExtent = 50.0f; //how far do we want to extent our trigger box
	const FVector2D cNodePos = FVector2D(currentNode->GetPosition().X, currentNode->GetPosition().Y);
	const FVector2D nNodePos = FVector2D(nextNode->GetPosition().X, nextNode->GetPosition().Y);
	const float distanceToNextNode = FVector2D::Distance(cNodePos, nNodePos);
	FVector nodeDifference = nextNode->GetPosition() - currentNode->GetPosition();
	nodeDifference.Normalize();


	FVector tBoxLoc = currentNode->GetPosition() + (nodeDifference * (distanceToNextNode / 2.0f)); //set to the middle point between two cover points
	tBoxLoc.Z += currentNode->GetHeight() / 2.0f; //move up

	FRotator tBoxFacingDirectionRot = nodeDifference.Rotation();
	
	//reverse rotation if object's scale is negative on X or Y axis
	if(objectsScale.X < 0.0f || objectsScale.Y < 0.0f)
		tBoxFacingDirectionRot += FRotator(0.0f, 90.0f, 0.0f);
	else
		tBoxFacingDirectionRot -= FRotator(0.0f, 90.0f, 0.0f);

	FVector tBoxFacingDirectionVec = tBoxFacingDirectionRot.Vector();
	tBoxFacingDirectionVec.Normalize();

	//FVector normals = currentNode->GetNormal() + nextNode->GetNormal();
	//normals.Normalize();
	//DrawDebugDirectionalArrow(_pWorld, tBoxLoc, tBoxLoc + normals * 100.0f, 5.0f, FColor::Red, true);//this would be a good method of finding corners
	DrawDebugDirectionalArrow(_pWorld, tBoxLoc, tBoxLoc + tBoxFacingDirectionVec * 20.0f, 5.0f, FColor::Yellow, true);	

	tBoxLoc += tBoxFacingDirectionVec * triggerBoxExtent;
	triggerBox->SetActorLocationAndRotation(tBoxLoc, nodeDifference.ToOrientationQuat());
	
	
	//SET SIZE
	if (UBoxComponent* triggerBoxShape = triggerBox->FindComponentByClass<UBoxComponent>())
	{
		triggerBoxShape->SetBoxExtent(FVector(distanceToNextNode / 2.0f, triggerBoxExtent, 10.0f));
	}
}

inline void CoverGen::OrganizeCoverNodesByDistance(CoverObject*& _coverObject)
{
	//auto nodes = _coverObject->GetAllCoverNodes();
	//
	//DrawDebugSphere(_pWorld, nodes[0]->GetPosition(), 5.0f, 11, FColor::Green, true);
	//DrawDebugSphere(_pWorld, nodes[0]->GetPosition(), 11.0f, 11, FColor::Magenta, true);
}

inline void CoverGen::OptimizeCoverNodes(CoverObject*& _coverObject, float _spacing)
{
	TArray<CoverNode*> Nodes;

	CoverNode* nodeZero = _coverObject->GetAllCoverNodes()[0];
	nodeZero->_bMainNode = true;
	Nodes.Add(nodeZero);
	float searchDistance = 0.1f;
	UE_LOG(LogTemp, Warning, TEXT("Object: %d"), _coverObject->_ID);
	DrawDebugSphere(_pWorld, nodeZero->GetPosition() + FVector::UpVector * 3.0f, 1.0f, 2, FColor::Red, true);
	float maxNormal = 0.1f;
	const int32 numberOfCoverNodes = _coverObject->GetAllCoverNodes().Num();
	
	int timeoutCheck = 0;
	while (Nodes.Num() < numberOfCoverNodes)
	{
		if (timeoutCheck > 5000)
		{
			UE_LOG(LogTemp, Warning, TEXT("There was a hole in the geometry. Trying to find a new base node."));

			//Find a different node as there's probably a hole in the geometry
			for (auto node : _coverObject->GetAllCoverNodes())
				if (!Nodes.Contains(node))
				{
					nodeZero = node;
					node->_bMainNode = true;
					timeoutCheck = 0;
					DrawDebugSphere(_pWorld, node->GetPosition() + FVector::UpVector * 3.0f, 1.0f, 2, FColor::Yellow, true);
					DrawDebugSphere(_pWorld, node->GetPosition() + FVector::UpVector * 100.0f, 1.0f, 2, FColor::Yellow, true);
					break;
				}


			if(timeoutCheck != 0)
			{
				UE_LOG(LogTemp, Error, TEXT("there was a hole in the geometry and no replacement for \"a new cover zero\" node was found in %s !" ), *(_coverObject->GetName()));
				DrawDebugSphere(_pWorld, _coverObject->GetLocation() + FVector::UpVector * 500.0f, 1.0f, 2, FColor::Red, true);
				return;
			}
		}

		timeoutCheck++;

		for (float index = 1; index < numberOfCoverNodes; ++index)
		{
			CoverNode* currentNode = _coverObject->GetAllCoverNodes()[index];
			float currentDistance  = FVector::Distance(nodeZero->GetPosition(), currentNode->GetPosition());
			FVector vNormal = nodeZero->GetNormal() - currentNode->GetNormal();
			
			if(currentDistance <=  searchDistance && !Nodes.Contains(currentNode) && NormalCheck2D(vNormal, maxNormal))
			{
				Nodes.Add(currentNode);
				//UE_LOG(LogTemp, Warning, TEXT("Distance = %f, searchDistance = %f, normal = %s"), currentDistance, searchDistance, *vNormal.ToString());
				searchDistance = 0.0f;
				maxNormal = 0.1f;
				timeoutCheck = 0;
				nodeZero->_bConnectedNode = true;
				nodeZero = currentNode;
				break;
			}
		}

		searchDistance += 0.1f;
		if (searchDistance > _spacing * 5.0f) { maxNormal += 0.5f; searchDistance = 0.1f; }
	}

	//fix index of all nodes <- CopyCoverNodes() is already doing this
	//for (int i = 0; i < Nodes.Num(); ++i)
	//	Nodes[i]->_iIndex = i;

	_coverObject->CopyCoverNodes(Nodes);
	Nodes.Empty();

	float const minDot = 0.6f;
	float const minHeightDifference = 0.001f;
	float const minZDifference = 5.0f;
	float const maxAcceptedDistance = _spacing * 2.0f;

	//Remove unnecessary nodes
	for (int index = 1; index < _coverObject->GetAllCoverNodes().Num() - 1; ++index)
	{
		CoverNode* pNode = _coverObject->GetAllCoverNodes()[index - 1];
		CoverNode* cNode = _coverObject->GetAllCoverNodes()[index];
		CoverNode* fNode = _coverObject->GetAllCoverNodes()[index + 1];

		float distanceToP = abs(FVector::DistXY(cNode->GetPosition(), pNode->GetPosition()));
		float distanceToF = abs(FVector::DistXY(cNode->GetPosition(), fNode->GetPosition()));
		float heightDifferenceP = abs(cNode->GetHeight() - pNode->GetHeight());
		float heightDifferenceF = abs(cNode->GetHeight() - fNode->GetHeight());
		float zDifferenceP = abs(pNode->GetPosition().Z - cNode->GetPosition().Z);
		float zDifferenceF = abs(fNode->GetPosition().Z - cNode->GetPosition().Z);

		float DotP = FVector::DotProduct(pNode->GetNormal(), cNode->GetNormal());
		float DotF = FVector::DotProduct(fNode->GetNormal(), cNode->GetNormal());

		//if (_coverObject->GetName() == "HouseCoverTest4" && cNode->_iIndex > 103 && cNode->_iIndex < 117)
		//{
		//	DrawDebugString(_pWorld, cNode->GetPosition() - FVector::UpVector * 10.0f, "C:" + FString::SanitizeFloat(cNode->GetHeight()));
		//	DrawDebugString(_pWorld, cNode->GetPosition() - FVector::UpVector * 20.0f, "P:" + FString::SanitizeFloat(heightDifferenceP));
		//	DrawDebugString(_pWorld, cNode->GetPosition() - FVector::UpVector * 30.0f, "F:" + FString::SanitizeFloat(heightDifferenceF));
		//}

		if (!Nodes.Contains(cNode) && !(cNode->_bMainNode))
			if (maxAcceptedDistance >= distanceToP && maxAcceptedDistance >= distanceToF)//distance check (X & Y axis only) to prevent gaps that are too long
				if (DotP > minDot && DotF > minDot)//normal - angle check
					if (heightDifferenceP < minHeightDifference && heightDifferenceF < minHeightDifference)//height difference check
						if (zDifferenceP < minZDifference && zDifferenceF < minZDifference)//Z axis difference check
							Nodes.Add(cNode);
	}

	for(auto node : Nodes)
		DrawDebugSphere(_pWorld, node->GetPosition(), 2.0f, 6, FColor::Red, true);

	_coverObject->RemoveCoverNodes(Nodes);

	//fix index of all nodes
	for (int i = 0; i < _coverObject->GetAllCoverNodes().Num(); ++i)
		_coverObject->GetAllCoverNodes()[i]->_iIndex = i;
	

	if (_coverObject->GetAllCoverNodes().Num() > 3)
	{
		for (int nodeIndex = 1; nodeIndex < _coverObject->GetAllCoverNodes().Num(); ++nodeIndex)
		{
			CoverNode* cNode = _coverObject->GetAllCoverNodes()[nodeIndex];
			CoverNode* pNode = _coverObject->GetAllCoverNodes()[nodeIndex - 1];
	
			//if (cNode->_iIndex > 103 && cNode->_iIndex < 117)
			{
				//DrawDebugSphere(_pWorld, cNode->GetPosition(), 6.0f, 2, FColor::Orange, true);
				//float _spacing_ = nodeIndex % 2 == 0 ? 20.0f : 30.0f;
				//FString tempTxt = FString::FromInt(pNode->_iIndex) + ">" + FString::FromInt(cNode->_iIndex);
				//DrawDebugString(_pWorld, pNode->GetPosition() + FVector::UpVector * _spacing_, tempTxt);
				
				if(pNode->_bConnectedNode)
				{
					FVector startTemp = pNode->GetPosition(); startTemp.Z = pNode->_fHeight;
					DrawDebugDirectionalArrow(_pWorld, startTemp + pNode->_VNormal, cNode->GetPosition() + cNode->_VNormal, 60.0f, FColor::Yellow, true);
				}
			}
		}
	}
}

inline void CoverGen::MargeNodesInProximity(CoverObject*& coverObject, float radius, bool margeOnlyNodesWithTheSameNormal)
{
#ifdef DebugMode
	DebugCheckForDuplicates(coverObject);
#endif // DebugMode

	TArray<CoverNode*> TestedNodes;
	TArray<CoverNode*> DuplicateNodes;

	for(int indexCurrentNode = 0; indexCurrentNode < coverObject->GetAllCoverNodes().Num(); ++indexCurrentNode)
	{
		CoverNode* cNodeCurrent = coverObject->GetAllCoverNodes()[indexCurrentNode];

		if(!DuplicateNodes.Contains(cNodeCurrent))
		{
			for(int indexTestedNode = indexCurrentNode + 1; indexTestedNode < coverObject->GetAllCoverNodes().Num(); ++indexTestedNode)
			{
				CoverNode* cNodeTested  = coverObject->GetAllCoverNodes()[indexTestedNode];

				if(!DuplicateNodes.Contains(cNodeTested))
				{
					float fDistance = FVector::Distance(cNodeCurrent->GetPosition(), cNodeTested->GetPosition());
					if( fDistance <= radius )
					{
						if(margeOnlyNodesWithTheSameNormal)
						{
							if(FVector::DotProduct(cNodeCurrent->GetNormal(), cNodeTested->GetNormal()) > 0.8f)
								DuplicateNodes.Add(cNodeTested);
						}

						else
						{
							DuplicateNodes.Add(cNodeTested);
						}
					}
				}
			}

			TestedNodes.Add(cNodeCurrent);
		}
	}

	coverObject->CopyCoverNodes(TestedNodes);

	for (auto node : DuplicateNodes)
		delete node;
}

inline void CoverGen::MargeNodesInProximity2D(CoverObject*& coverObject, float radius)
{
#ifdef DebugMode
	DebugCheckForDuplicates(coverObject);
#endif // DebugMode


	TArray<CoverNode*> TestedNodes;
	TArray<CoverNode*> DuplicateNodes;

	for (int indexCurrentNode = 0; indexCurrentNode < coverObject->GetAllCoverNodes().Num(); ++indexCurrentNode)
	{
		CoverNode* cNodeCurrent  = coverObject->GetAllCoverNodes()[indexCurrentNode];
		FVector2D currentNodePos = { cNodeCurrent->GetPosition().X, cNodeCurrent->GetPosition().Y };

		if (!DuplicateNodes.Contains(cNodeCurrent))
		{
			for (int indexTestedNode = indexCurrentNode + 1; indexTestedNode < coverObject->GetAllCoverNodes().Num(); ++indexTestedNode)
			{
				CoverNode* cNodeTested  = coverObject->GetAllCoverNodes()[indexTestedNode];
				FVector2D testedNodePos = { cNodeTested->GetPosition().X, cNodeTested->GetPosition().Y };

				if (!DuplicateNodes.Contains(cNodeTested) && floor(cNodeCurrent->GetPosition().Z) == floor(cNodeTested->GetPosition().Z))
				{
					float fDistance = FVector2D::Distance(currentNodePos, testedNodePos);
					if (fDistance <= radius)
					{
						DuplicateNodes.Add(cNodeTested);
					}
				}
			}

			TestedNodes.Add(cNodeCurrent);
		}
	}

	coverObject->CopyCoverNodes(TestedNodes);

	for (auto node : DuplicateNodes)
		delete node;
}

inline void CoverGen::DebugDrawAllCoverNodes()
{
	if(allCoverObjects)
	{
		// Dynamic nodes
		for(auto dynamicCoverObject : allCoverObjects->DynamicCoverObjects)
		{
			//if(FVector::Distance((_pWorld->GetFirstPlayerController()->GetActorLocation()), dynamicCoverObject->GetLocation()) < 1000.0f)
			{
				int index = 0;
				for(auto dynamicCoverNode : dynamicCoverObject->GetAllCoverNodes())
				{
					//Draw Node
					DrawDebugSphere(_pWorld, dynamicCoverNode->GetPosition(), 2.5f, 5, FColor::Green, true);
					//Draw normal
					DrawDebugDirectionalArrow  (_pWorld, dynamicCoverNode->GetPosition(), dynamicCoverNode->GetPosition() + dynamicCoverNode->GetNormal() * 10.0f, 5.0f, FColor::Yellow, true);
					//Draw height
					FVector HeightVec = FVector(dynamicCoverNode->GetPosition().X, dynamicCoverNode->GetPosition().Y, dynamicCoverNode->GetPosition().Z + dynamicCoverNode->GetHeight());
					DrawDebugLine(_pWorld, dynamicCoverNode->GetPosition() + dynamicCoverNode->GetNormal() * 2.0f, HeightVec, FColor::Green, true);
					DrawDebugSphere(_pWorld, HeightVec, 2.5f, 2, FColor::Green, true);

					/*
					if(index != 0 && dynamicCoverObject->GetAllCoverNodes()[index - 1]->_bConnectedNode )
					{
						CoverNode* PrevNode    = dynamicCoverObject->GetAllCoverNodes()[index - 1];
						CoverNode* tallerNode  = dynamicCoverNode->_fHeight < PrevNode->_fHeight ? PrevNode : dynamicCoverNode;
						CoverNode* shorterNode = dynamicCoverNode == tallerNode ? PrevNode : dynamicCoverNode;
						
						for(float heightOffeset = 0.0f; heightOffeset <= tallerNode->_fHeight; heightOffeset += 0.1f)
						{
							FVector startPos = tallerNode->GetPosition(); startPos.Z = tallerNode->GetPosition().Z  + heightOffeset;
							FVector endPos   = shorterNode->GetPosition();

							if (heightOffeset > shorterNode->_fHeight)
								endPos.Z = shorterNode->GetPosition().Z + shorterNode->_fHeight;

							else
								endPos.Z = shorterNode->GetPosition().Z + heightOffeset;

							startPos += tallerNode->_VNormal  * 10.0f;
							endPos   += shorterNode->_VNormal * 10.0f;

							DrawDebugLine(_pWorld, startPos, endPos, FColor::Green, true);
						}

					
					}

					index++;
					*/
				}
			}
		}


		// Static nodes
		for (auto staticCoverObject : allCoverObjects->StaticCoverObjects)
		{
			int index = 0;
			for (auto staticCoverNode : staticCoverObject->GetAllCoverNodes())
			{
				//Draw Node
				DrawDebugSphere(_pWorld, staticCoverNode->GetPosition(), 2.5f, 5, FColor::Blue, true);
				//Draw normal
				DrawDebugDirectionalArrow  (_pWorld, staticCoverNode->GetPosition(), staticCoverNode->GetPosition() + staticCoverNode->GetNormal() * 10.0f, 5.0f, FColor::Yellow, true);
				//Draw height
				FVector HeightVec = FVector(staticCoverNode->GetPosition().X, staticCoverNode->GetPosition().Y, staticCoverNode->GetPosition().Z + staticCoverNode->GetHeight());
				DrawDebugLine(_pWorld, staticCoverNode->GetPosition() + staticCoverNode->GetNormal() * 2.0f, HeightVec, FColor::Blue, true);
				DrawDebugSphere(_pWorld, HeightVec, 2.5f, 2, FColor::Blue, true);
				//DrawDebugString(_pWorld, staticCoverNode->GetPosition(), HeightVec.ToString());
				index++;
			}
		}
	}
}

inline void CoverGen::FindEdgeLink(int& currentIndex, TArray<FVector>& vertices, TArray<CoverGen::Edge2*>& edgesOut, FVector& V0, FVector& V1, FVector& V2, FVector triangleNormal, bool ignoreSurfacesWithVerticalFaces)
{
	if (vertices[currentIndex] == V0)
	{
		for (int vertIndex2 = currentIndex + 1; vertIndex2 < vertices.Num(); ++vertIndex2)
		{
			if (vertices[vertIndex2] == V1)
			{
				if((triangleNormal.Z < 0.8f && triangleNormal.Z > -0.8f) || !ignoreSurfacesWithVerticalFaces)
				{
					FVector linkDirection = V1 - V0;
					linkDirection.Normalize();

					if((linkDirection.Z < 0.8f && linkDirection.Z > -0.8f) || !ignoreSurfacesWithVerticalFaces)
					{
						Edge2* tempEdge = new Edge2(V0, V1, triangleNormal, linkDirection);
						edgesOut.Add(tempEdge);
					}
				}
				break;
			}

			if (vertices[vertIndex2] == V2)
			{
				if ((triangleNormal.Z < 0.8f && triangleNormal.Z > -0.8f) || !ignoreSurfacesWithVerticalFaces)
				{
					FVector linkDirection = V2 - V0;
					linkDirection.Normalize();

					if ((linkDirection.Z < 0.8f && linkDirection.Z > -0.8f) || !ignoreSurfacesWithVerticalFaces)
					{
						Edge2* tempEdge = new Edge2(V0, V2, triangleNormal, linkDirection);
						edgesOut.Add(tempEdge);
					}
				}
				break;
			}
		}
	}
}

inline void CoverGen::CreateEdgeLinks(const TArray<FVector>& triangles, TArray<FVector>& vertices, TArray<CoverGen::Edge2*>& edgesOut)
{
	for(int vertIndex1 = 0; vertIndex1 < vertices.Num() - 1; ++vertIndex1)
	{
		for(int V = 2; V < triangles.Num(); V += 3)
		{
			//Single Triangle
			FVector V0 = triangles[V - 0];
			FVector V1 = triangles[V - 1];
			FVector V2 = triangles[V - 2];
			FVector vNormal = CalculateSurfaceNormalOfATriangle(V0, V1, V2);
			vNormal.Normalize();

			// If V0 is our vertex point
			if (vertices[vertIndex1] == V0)
				FindEdgeLink(vertIndex1, vertices, edgesOut, V0, V1, V2, vNormal, true);

			// If V1 is our vertex point
			else if (vertices[vertIndex1] == V1)
				FindEdgeLink(vertIndex1, vertices, edgesOut, V1, V0, V2, vNormal, true);

			// If V2 is our vertex point
			else if (vertices[vertIndex1] == V2)
				FindEdgeLink(vertIndex1, vertices, edgesOut, V2, V0, V1, vNormal, true);
		}
	}
}

inline bool CoverGen::isVecHeightInBounds(const float& boundingBoxBottom, FVector& vec, float min, float max)
{
	if (boundingBoxBottom + min > vec.Z)
		return false;

	if (boundingBoxBottom + max < vec.Z)
		return false;

	return true;
}

CoverGen::CoverNode* CoverGen::CoverObject::AddNewCoverPoint(FVector nodePosition, FVector nodeNormal)
{
	CoverNode* tempNode = new CoverNode(_coverNodes.Num(), nodePosition, nodeNormal);
	//  use size of the array (_coverNodes.Num()) to pass the index
	_coverNodes.Add(tempNode);

	return tempNode;
}

void CoverGen::CoverObject::CopyCoverNodes(TArray<CoverNode*>& copyFrom)
{
	_coverNodes.Empty();
	int index = 0;
	
	for (auto node : copyFrom)
	{
		_coverNodes.Add(node);
		node->_iIndex = index;
		index++;
	}
}

TArray<CoverGen::CoverNode*> CoverGen::CoverObject::GetTheLowestChainOfNodes(float spacing)
{
	TArray<CoverNode*> result;
	TArray<FVector2D> AllXY;

	//get all unique X and Y position in the array
	for(auto node : _coverNodes)
	{
		bool matchFound = false;
		for(auto pos2D : AllXY)
			if (FVector2D::Distance(pos2D, FVector2D(node->GetPosition().X, node->GetPosition().Y)) < spacing - 1.0f)
			{
				matchFound = true;
				break;
			}

		if (!matchFound)
		{
			AllXY.Add(FVector2D(node->GetPosition().X, node->GetPosition().Y));
			matchFound = false;
		}
	}

	//get lowest nodes on each X & Y (because we start ray cast from the bottom the first node in the array is guaranteed to be the lowest)
	for(FVector2D pos : AllXY)
		for (auto node : _coverNodes)
			if (FVector2D::Distance(pos, FVector2D(node->GetPosition().X, node->GetPosition().Y)) < spacing / 2.0f)
			{
				result.Add(node);
				break;
			}


	return result;
}

void CoverGen::CoverObject::OrganizeNodeArrayByLocation()
{
	//Find unique normals
	TArray<FVector> arrayOfUniqueNormals;
	for (CoverNode* node : _coverNodes)
	{
		if(!(arrayOfUniqueNormals.Contains(node->_VNormal)))
		{
			arrayOfUniqueNormals.Add(node->_VNormal);
		}
	}

	TArray<CoverNode*> organizedCoverNodes;
	for(FVector uniqueNormal : arrayOfUniqueNormals)
	{
		for(CoverNode* node : _coverNodes)
		{
			if (uniqueNormal == node->_VNormal)
			{
				node->_iIndex = organizedCoverNodes.Num();
				organizedCoverNodes.Add(node);
			}
		}
	}

	CopyCoverNodes(organizedCoverNodes);

	//for(int mainNodeIndex = 0; mainNodeIndex < _coverNodes.Num() - 1; ++mainNodeIndex)
	//	for(int testedNodeIndex = mainNodeIndex + 1; testedNodeIndex < _coverNodes.Num(); ++testedNodeIndex)
	//		if(_coverNodes[mainNodeIndex]->GetNormal() == _coverNodes[testedNodeIndex]->GetNormal())
	//		{
	//			_coverNodes[mainNodeIndex]->_iIndex = testedNodeIndex;
	//			_coverNodes[testedNodeIndex]->_iIndex = mainNodeIndex;
	//			_coverNodes.Swap(mainNodeIndex, testedNodeIndex);
	//		}
}

void CoverGen::CoverObject::RemoveCoverNodes(TArray<CoverNode*>& nodesToBeRemoved)
{
	TArray<CoverNode*> newNodeArray;

	for(auto node : _coverNodes)
	{
		if (!nodesToBeRemoved.Contains(node))
			newNodeArray.Add(node);
	}

	for (auto nodeDelete : nodesToBeRemoved)
		delete nodeDelete;

	_coverNodes.Empty();
	_coverNodes = newNodeArray;
}
