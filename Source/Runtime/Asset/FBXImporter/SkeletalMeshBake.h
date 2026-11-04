#pragma once

#include <filesystem>

struct FFbxCookedData;

class FSkeletalMeshBake
{
public:
	static bool ReadBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, FFbxCookedData& OutData);
	static void WriteBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, const FFbxCookedData& Data);
};
