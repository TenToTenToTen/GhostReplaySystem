// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InstancedStructUtils.generated.h"

struct FInstancedStruct;

/**
 * 
 */
UCLASS()
class BLOODSTAINSYSTEM_API UInstancedStructUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="BloodStain|InstancedStructUtils")
	static bool AreInstancedStructsSameType(const FInstancedStruct& A, const FInstancedStruct& B);
};
