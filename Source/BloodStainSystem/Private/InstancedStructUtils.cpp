/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "InstancedStructUtils.h"

#include "StructUtils/InstancedStruct.h"

bool UInstancedStructUtils::AreInstancedStructsSameType(const FInstancedStruct& A, const FInstancedStruct& B)
{
	return A.GetScriptStruct() == B.GetScriptStruct();
}