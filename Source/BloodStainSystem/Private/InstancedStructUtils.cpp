// Fill out your copyright notice in the Description page of Project Settings.


#include "InstancedStructUtils.h"

#include "StructUtils/InstancedStruct.h"

bool UInstancedStructUtils::AreInstancedStructsSameType(const FInstancedStruct& A, const FInstancedStruct& B)
{
	return A.GetScriptStruct() == B.GetScriptStruct();
}