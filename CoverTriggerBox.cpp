// Fill out your copyright notice in the Description page of Project Settings.


#include "CoverTriggerBox.h"
#include "DrawDebugHelpers.h"
#include "Components/BoxComponent.h"

ACoverTriggerBox::ACoverTriggerBox()
{
	UE_LOG(LogTemp, Warning, TEXT("CoverTriggerBox Created"));
	OnActorBeginOverlap.AddDynamic(this, &ACoverTriggerBox::OnOverlapBegin);
	OnActorEndOverlap.AddDynamic(this, &ACoverTriggerBox::OnOverlapEnd);
}

ACoverTriggerBox::~ACoverTriggerBox()
{
	UE_LOG(LogTemp, Warning, TEXT("CoverTriggerBox Destroyed"));
}

void ACoverTriggerBox::DebugDrawTriggerBox()
{
	//UBoxComponent* triggerBoxShape = FindComponentByClass<UBoxComponent>();

	if (UBoxComponent* triggerBoxShape = FindComponentByClass<UBoxComponent>())
		DrawDebugBox(GetWorld(), GetActorLocation(), triggerBoxShape->GetScaledBoxExtent(), GetActorQuat(), FColor::Purple, true, -1, 0, 1.0f);
}

void ACoverTriggerBox::BeginPlay()
{
	Super::BeginPlay();

	//UBoxComponent* triggerBoxShape = FindComponentByClass<UBoxComponent>();
	//
	//if(triggerBoxShape)
	//	DrawDebugBox(GetWorld(), GetActorLocation(), triggerBoxShape->GetScaledBoxExtent(), GetActorQuat(), FColor::Purple, true, -1, 0, 2.0f);

}

void ACoverTriggerBox::OnOverlapBegin(AActor* OverlappedActor, AActor* OtherActor)
{
}

void ACoverTriggerBox::OnOverlapEnd(AActor* OverlappedActor, AActor* OtherActor)
{
}
