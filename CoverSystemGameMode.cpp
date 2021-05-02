// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoverSystemGameMode.h"
#include "CoverSystemCharacter.h"
#include "UObject/ConstructorHelpers.h"

ACoverSystemGameMode::ACoverSystemGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPersonCPP/Blueprints/ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
