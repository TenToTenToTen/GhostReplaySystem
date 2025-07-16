/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "BloodStainFileOptions.h"

FArchive& operator<<(FArchive& Ar, FCompressionOption& Options)
{
	Ar << Options.Method;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBloodStainFileOptions& Options)
{
	Ar << Options.Compression;
	Ar << Options.Quantization;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBloodStainFileHeader& Header)
{
	Ar << Header.Magic;
	Ar << Header.Version;
	Ar << Header.Options;
	Ar << Header.UncompressedSize;
	return Ar;
}
