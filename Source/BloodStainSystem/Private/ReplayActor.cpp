/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "ReplayActor.h"

AReplayActor::AReplayActor()
{
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = Root;
	PrimaryActorTick.bCanEverTick = false;
}

