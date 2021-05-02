// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"
#include "CoverTriggerBox.generated.h"

/**
 * 
 */
UCLASS()
class COVERSYSTEM_API ACoverTriggerBox : public ATriggerBox
{
	GENERATED_BODY()
	
protected:
	virtual void BeginPlay() override;

public:
	ACoverTriggerBox();
	~ACoverTriggerBox();

	void DebugDrawTriggerBox();

	UFUNCTION()
	void OnOverlapBegin(class AActor* OverlappedActor, class AActor* OtherActor);

	UFUNCTION()
	void OnOverlapEnd(class AActor* OverlappedActor, class AActor* OtherActor);
};
