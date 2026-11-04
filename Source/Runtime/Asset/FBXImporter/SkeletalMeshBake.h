#pragma once

#include <filesystem>

struct FSkeletalMeshData;

class FSkeletalMeshBake
{
public:
	static bool ReadBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, FSkeletalMeshData& OutData);
	static void WriteBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, const FSkeletalMeshData& Data);
};
