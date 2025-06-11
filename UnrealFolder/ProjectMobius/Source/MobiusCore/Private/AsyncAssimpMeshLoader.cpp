// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

#include "AsyncAssimpMeshLoader.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/material.h"
#include "assimp/texture.h"
#include "assimp/postprocess.h"
#include "Kismet/KismetMathLibrary.h"


UAsyncAssimpMeshLoader::UAsyncAssimpMeshLoader()
{
}

TArray<FIntVector> UAsyncAssimpMeshLoader::TriangulateWktPolygon(const TArray<FVector2D>& Polygon,
                                                                 TArray<FVector>& OutVertices)
{
	TArray<FIntVector> Triangles;

	if (Polygon.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("Polygon must have at least 3 points."));
		return Triangles;
	}

	// Generate OBJ data string
	FString OBJ = TEXT("o WKTPolygon\n");
	for (const FVector2D& P : Polygon)
	{
		OBJ += FString::Printf(TEXT("v %f %f 0.0\n"), P.X, P.Y);
	}
	OBJ += TEXT("f");
	for (int32 i = 1; i <= Polygon.Num(); ++i)
	{
		OBJ += FString::Printf(TEXT(" %d"), i);
	}
	OBJ += TEXT("\n");

	std::string OBJData = TCHAR_TO_UTF8(*OBJ);
	Assimp::Importer Importer;
	const aiScene* Scene = Importer.ReadFileFromMemory(
		OBJData.c_str(), OBJData.size(),
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices,
		"obj");

	if (!Scene || !Scene->HasMeshes())
	{
		UE_LOG(LogTemp, Error, TEXT("Assimp failed to triangulate: %s"), UTF8_TO_TCHAR(Importer.GetErrorString()));
		return Triangles;
	}

	const aiMesh* Mesh = Scene->mMeshes[0];
	OutVertices.Empty();
	for (unsigned int i = 0; i < Mesh->mNumVertices; ++i)
	{
		const aiVector3D& V = Mesh->mVertices[i];
		OutVertices.Add(FVector(V.x, V.y, V.z));
	}

	for (unsigned int i = 0; i < Mesh->mNumFaces; ++i)
	{
		const aiFace& Face = Mesh->mFaces[i];
		if (Face.mNumIndices == 3)
		{
			Triangles.Add(FIntVector(Face.mIndices[0], Face.mIndices[1], Face.mIndices[2]));
		}
	}

	return Triangles;
}

FAssimpMeshLoaderRunnable::FAssimpMeshLoaderRunnable(const FString InPathToMesh)
{
	if(InPathToMesh.IsEmpty())
	{
		
		return;
	}
	else if(!FPaths::FileExists(InPathToMesh))
	{
		// if the path to the mesh is not a valid file path and the string is not an obj string then return
		UE_LOG(LogTemp, Warning, TEXT("The path to the mesh is not a valid file path: %s"), *InPathToMesh);
		return;
	}
	
	PathToMesh = InPathToMesh;
	// if file has .wkt extension then it is a WKT file
	bIsWktExtension = PathToMesh.EndsWith(TEXT(".wkt"), ESearchCase::IgnoreCase);
	

	// Create the thread -- The thread priority is set to TPri_Normal this may need to be adjusted based on the application
	Thread = FRunnableThread::Create(this, TEXT("FAssimpMeshLoaderRunnable"), 0, TPri_Normal);
}

FAssimpMeshLoaderRunnable::~FAssimpMeshLoaderRunnable()
{
	// if the thread is still running, stop it
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

uint32 FAssimpMeshLoaderRunnable::Run()
{
	if (bIsWktExtension)
	{
		ProcessMeshFromString();
	}
	else
	{
		ProcessMeshFromFile();
	}

	// sleep the thread for 0.5 seconds
	FPlatformProcess::Sleep(0.5f);

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		// Broadcast complete
		OnLoadMeshDataComplete.Broadcast();
	});
	
	return 0;
}

void FAssimpMeshLoaderRunnable::Stop()
{
	bShouldStop = true;
}

void FAssimpMeshLoaderRunnable::Exit()
{
	FRunnable::Exit();
}

void FAssimpMeshLoaderRunnable::ProcessMeshFromFile()
{
	// Broadcast the current percentage of the data loaded as 0 this way the ui will show

	Assimp::Importer Importer;
	const std::string Filename(TCHAR_TO_UTF8(*PathToMesh));
	const aiScene* Scene = Importer.ReadFile(Filename, aiProcess_MakeLeftHanded | aiProcess_FlipUVs |
	                                         aiProcess_PreTransformVertices | aiProcess_Triangulate |
	                                         aiProcess_GenNormals | aiProcess_CalcTangentSpace);

	if (!Scene)
	{
		ErrorMessageCode = Importer.GetErrorString();
		return;
	}

	if (!Scene->HasMeshes())
	{
		ErrorMessageCode = "The scene does not have any meshes";
		return;
	}

	FillDataFromScene(Scene);
}

void FAssimpMeshLoaderRunnable::ProcessMeshFromString()
{
	LoadWKTDataToObjString();
	std::string OBJData = TCHAR_TO_UTF8(*WktDataString);
	Assimp::Importer Importer;
	const aiScene* Scene = Importer.ReadFileFromMemory(
		OBJData.c_str(), OBJData.size(),
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals | aiProcess_CalcTangentSpace,
		//TODO: Need to workout a way to handle normals for WKT data -> filters just don't work for this
		"obj");

	if (!Scene || !Scene->HasMeshes())
	{
		UE_LOG(LogTemp, Error, TEXT("Assimp failed to triangulate: %s"), UTF8_TO_TCHAR(Importer.GetErrorString()));
		return;
	}

	FillDataFromScene(Scene);
}

void FAssimpMeshLoaderRunnable::LoadWKTDataToObjString()
{
	// 1) Load the raw WKT (JSON-wrapped) from disk
	FString RawWkt;
	if (!LoadWKTFile(PathToMesh, RawWkt, ErrorMessage))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load WKT file: %s"), *ErrorMessage);
		return;
	}

	// 2) Parse into one or more polygons
	TArray<TArray<FVector2D>> Geometries;
	if (!ParseGeometryCollectionWkt(RawWkt, Geometries, ErrorMessage))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to parse geometry: %s"), *ErrorMessage);
		return;
	}

	// 3) Begin building the OBJ string
	WktDataString.Empty();
	int32 VertexOffset = 0;  // keep track of 1-based OBJ indexing

	for (const TArray<FVector2D>& Geometry : Geometries)
	{
		const int32 NumPts = Geometry.Num();
		if (NumPts < 3) continue;

		// keep track of where our base vertices start in the global OBJ index
		const int32 BaseStart = VertexOffset;

		// 1) emit the base ring (Z = 0)
		for (const FVector2D& P : Geometry)
		{
			// assuming input is in meters, scale to centimeters for OBJ (multiply by 100)
			FVector V(P.X * 100.0f, P.Y * 100.0f, 0.0f);
			WktDataString += FString::Printf(TEXT("v %f %f %f\n"), V.X, V.Y, V.Z);
		}

		// 2) cap *that* ring immediately (so it can only ever seal the floor)
		WktDataString += TEXT("f");
		for (int32 i = 0; i < NumPts; ++i)
		{
			// OBJ is 1-based: BaseStart+1 … BaseStart+NumPts
			WktDataString += FString::Printf(TEXT(" %d"), BaseStart + i + 1);
		}
		WktDataString += TEXT("\n");

		// 3) emit the top ring (Z = 100)
		const int32 TopStart = BaseStart + NumPts;
		for (const FVector2D& P : Geometry)
		{
			FVector V(P.X * 100.0f, P.Y * 100.0f, 100.0f);
			WktDataString += FString::Printf(TEXT("v %f %f %f\n"), V.X, V.Y, V.Z);
		}

		// 4) now build double-sided walls
		for (int32 i = 0; i < NumPts; ++i)
		{
			const int32 A    = BaseStart + i + 1;
			const int32 B    = BaseStart + ((i + 1) % NumPts) + 1;
			const int32 ATop = TopStart  + i + 1;
			const int32 BTop = TopStart  + ((i + 1) % NumPts) + 1;

			// outer
			WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A,    B,    BTop);
			WktDataString += FString::Printf(TEXT("f %d %d %d\n"), A,    BTop, ATop);

			// inner (reversed)
			WktDataString += FString::Printf(TEXT("f %d %d %d\n"), BTop, B,    A);
			WktDataString += FString::Printf(TEXT("f %d %d %d\n"), ATop, BTop, A);
		}

		// advance for next polygon
		VertexOffset += NumPts * 2;
	}
}


bool FAssimpMeshLoaderRunnable::LoadWKTFile(const FString& FilePath, FString& OutWKTData, FString& OutErrorMessage)
{
	// Check if the file exists
	if (!FPaths::FileExists(FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}

	// Load the file content
	if (FFileHelper::LoadFileToString(OutWKTData, *FilePath))
	{
		// Successfully loaded the file
		return true;
	}

	// failed to load the file and parse as string
	OutErrorMessage = FString::Printf(TEXT("Failed to load WKT file: %s"), *FilePath);

	// failed to load the file
	return false;
}

TArray<FVector2D> FAssimpMeshLoaderRunnable::ParseWKTData(const FString& InWKTDataString, FString& OutErrorMessage)
{
	FString CleanWKT = InWKTDataString;
	CleanWKT.TrimStartAndEndInline();
	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));

	FString Prefix;
	FString CoordBlock;

	// Extract prefix and inner coordinates
	int32 OpenParenIndex;
	if (CleanWKT.FindChar('(', OpenParenIndex))
	{
		Prefix = CleanWKT.Left(OpenParenIndex).ToUpper().TrimStartAndEnd();
		CoordBlock = CleanWKT.Mid(OpenParenIndex);
		CoordBlock = CoordBlock.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT(""));
	}

	TArray<FVector2D> ParsedPoints;

	if (Prefix == TEXT("POINT"))
	{
		TArray<FString> XY;
		CoordBlock.ParseIntoArray(XY, TEXT(" "), true);
		if (XY.Num() == 2)
		{
			ParsedPoints.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
		}
	}
	else if (Prefix == TEXT("LINESTRING") || Prefix == TEXT("POLYGON"))
	{
		if (Prefix == TEXT("POLYGON"))
		{
			// POLYGON can have nested parentheses
			int32 InnerStart = CleanWKT.Find(TEXT("(("));
			int32 InnerEnd = CleanWKT.Find(TEXT("))"));
			if (InnerStart != INDEX_NONE && InnerEnd != INDEX_NONE)
			{
				CoordBlock = CleanWKT.Mid(InnerStart + 2, InnerEnd - InnerStart - 2);
			}
		}

		TArray<FString> Pairs;
		CoordBlock.ParseIntoArray(Pairs, TEXT(","), true);
		for (const FString& Pair : Pairs)
		{
			TArray<FString> XY;
			Pair.TrimStartAndEnd().ParseIntoArray(XY, TEXT(" "), true);
			if (XY.Num() == 2)
			{
				ParsedPoints.Add(FVector2D(FCString::Atof(*XY[0]), FCString::Atof(*XY[1])));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Unsupported WKT type: %s"), *Prefix);
		OutErrorMessage = FString::Printf(TEXT("Unsupported WKT type: %s"), *Prefix);
	}

	return ParsedPoints;
}

bool FAssimpMeshLoaderRunnable::ParseGeometryCollectionWkt(const FString& WKTString,
                                                           TArray<TArray<FVector2D>>& OutGeometries, FString& OutErrorMessage)
{
	FString CleanWKT = WKTString;
	CleanWKT.TrimStartAndEndInline();
	CleanWKT = CleanWKT.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));

	if (!CleanWKT.StartsWith(TEXT("GEOMETRYCOLLECTION"), ESearchCase::IgnoreCase))
	{
		OutErrorMessage = TEXT("WKT does not begin with GEOMETRYCOLLECTION");
		return false;
	}

	// Extract the content inside the GEOMETRYCOLLECTION (...)
	int32 OpenParen = CleanWKT.Find(TEXT("("));
	int32 CloseParen = INDEX_NONE;
	if (OpenParen == INDEX_NONE || !CleanWKT.FindLastChar(')', CloseParen) || CloseParen <= OpenParen)
	{
		OutErrorMessage = TEXT("Malformed GEOMETRYCOLLECTION WKT.");
		return false;
	}

	FString Inner = CleanWKT.Mid(OpenParen + 1, CloseParen - OpenParen - 1).TrimStartAndEnd();

	// Look for each POLYGON block
	int32 Pos = 0;
	while (true)
	{
		int32 PolygonStart = Inner.Find(TEXT("POLYGON"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
		if (PolygonStart == INDEX_NONE) break;

		int32 FirstParen = Inner.Find(TEXT("(("), ESearchCase::IgnoreCase, ESearchDir::FromStart, PolygonStart);
		int32 EndParen = Inner.Find(TEXT("))"), ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstParen + 2);
		if (FirstParen == INDEX_NONE || EndParen == INDEX_NONE) break;

		FString PolygonBlock = Inner.Mid(FirstParen + 2, EndParen - FirstParen - 2);

		FString DummyError;
		TArray<FVector2D> PolygonPoints = ParseWKTData(TEXT("LINESTRING(") + PolygonBlock + TEXT(")"), DummyError);
		if (!PolygonPoints.IsEmpty())
		{
			OutGeometries.Add(PolygonPoints);
		}

		Pos = EndParen + 2;
	}

	if (OutGeometries.Num() == 0)
	{
		OutErrorMessage = TEXT("No valid POLYGON found in GEOMETRYCOLLECTION.");
		return false;
	}

	return true;
}

FRotator FAssimpMeshLoaderRunnable::GetMeshRotation(int32 AxisUpOrientation, int32 AxisUpSign,
                                                    int32 AxisForwardOrientation, int32 AxisForwardSign)
{

	static const FRotator UpRotation[4][3] =
	{
		{// sign unknown assume zero
			{ // sign unknown assume 
				FRotator::ZeroRotator
			},
			{ // positive
				FRotator::ZeroRotator
			},
			{ // negative
				FRotator::ZeroRotator
			}
		},
		{ // X is up
			{ // sign unknown assume positive x 
				FRotator(90.0f, 0.0f, 0.0f)
			},
			{ // positive
				FRotator(90.0f, 0.0f, 0.0f)
			},
			{ // negative
				FRotator(-90.0f, 0.0f, 0.0f)
			}
		},
		{ // y is up
			{ // sign unknown assume 
				FRotator(0.0f, 0.0f, -90.0f)
			},
			{ // positive
				FRotator(0.0f, 0.0f, -90.0f)
			},
			{ // negative
				FRotator(0.0f, 0.0f, 90.0f)
			}
		},
		{ // Z is up
			{ // sign unknown assume positive
				FRotator::ZeroRotator
			},
			{ // positive
				FRotator::ZeroRotator
			},
			{ // negative
				FRotator(180.0f, 0.0f, 0.0f)
			}
		}
	};
	
	int32 AxisOrientationPicker = AxisUpOrientation == -1 ? 0 : AxisUpOrientation;
	int32 AxisUpSignPicker = AxisUpSign == -1 ? 2 : AxisUpSign;

	// make rotator based on the up axis orientation and sign
	FRotator ReturnRotation = UpRotation[AxisOrientationPicker][AxisUpSignPicker];
	
	// modify the rotation based on the forward axis orientation and sign
	if(AxisForwardOrientation != 0)
	{
		// modify the rotation based on the forward axis orientation and sign
		FRotator ForwardRotation = FRotator::ZeroRotator;
		switch (AxisForwardOrientation)
		{
		case 1:// X
			switch (AxisUpOrientation)
			{
			case 1:
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				break;
			case 2: // Y
				if(AxisUpSign == -1)
				{
					ForwardRotation = FRotator(0.0f, -90.0f, 0.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 90.0f, 0.0f);
				}
				break;
			case 3: // Z
				if(AxisUpSign == -1)
				{
					ForwardRotation = FRotator(0.0f, 0.0f, 90.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
				}
				break;
			default: // the up axis is unknown so use the forward axis
				if(AxisForwardSign == -1)
				{
					ForwardRotation = FRotator(0.0f, 0.0f, 90.0f);
				}
				else
				{
					ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
				}
				break;
			}
			break;
		case 2:// Y
			ForwardRotation = FRotator(0.0f, 0.0f, -90.0f);
			break;
		case 3:// Z
			ForwardRotation = FRotator::ZeroRotator;
			break;
		default:// unknown
			break;
		}
		// modify the rotation based on the forward axis sign
		if(AxisForwardSign == -1)
		{
			ReturnRotation += ForwardRotation;
		}
		else
		{
			ReturnRotation -= ForwardRotation;
		}
	}

	
	return ReturnRotation;
}

FVector FAssimpMeshLoaderRunnable::TransformNormal(const FVector& InNormal, int32 AxisUpOrientation, int32 AxisForwardOrientation, int32 AxisForwardSign, int32 AxisUpSign)
{
	FMatrix TransformMatrix = FMatrix::Identity;

	// Define transformation matrix based on up and forward axes
	switch (AxisUpOrientation)
	{
	case 1: // X up
		switch (AxisForwardOrientation)
		{
		case 2: // Y forward
			TransformMatrix = FMatrix(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector::ZeroVector);
			break;
		case 3: // Z forward
			TransformMatrix = FMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector);
			break;
		}
		break;

	case 2: // Y up
		switch (AxisForwardOrientation)
		{
		case 1: // X forward
			TransformMatrix = FMatrix(FVector(0, 1, 0), FVector(1, 0, 0), FVector(0, 0, 1), FVector::ZeroVector);
			break;
		case 3: // Z forward
			TransformMatrix = FMatrix(FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), FVector::ZeroVector);
			break;
		}
		break;

	case 3: // Z up
		switch (AxisForwardOrientation)
		{
		case 1: // X forward
			TransformMatrix = FMatrix(FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector);
			break;
		case 2: // Y forward
			TransformMatrix = FMatrix(FVector(0, 0, 1), FVector(0, 1, 0), FVector(1, 0, 0), FVector::ZeroVector);
			break;
		}
		break;

	default:
		break;
	}

	// Invert and transpose the matrix for normal transformations
	FMatrix NormalTransformMatrix = TransformMatrix.Inverse().GetTransposed();

	// Transform the normal
	FVector TransformedNormal = NormalTransformMatrix.TransformVector(InNormal);

	// Flip directions based on signs
	if (AxisForwardSign == -1)
	{
		TransformedNormal = FRotator(0.0f, 180.0f, 0.0f).RotateVector(TransformedNormal);
	}

	TransformedNormal.X *= AxisForwardSign;
	TransformedNormal.Z *= AxisUpSign;

	// Normalize to maintain proper orientation
	return TransformedNormal.GetSafeNormal();
}
//TODO: this method needs to be refactored to use matrix transformations as it doesn't work when it comes to normals and this is where the issue is for translucent materials
void FAssimpMeshLoaderRunnable::TransformMeshMatrix(FVector& InVector, int32 AxisUpOrientation, int32 AxisUpSign,
                                                    int32 AxisForwardOrientation, int32 AxisForwardSign)
{
	

	// manipulate the vector based on the up axis and the forward axis
	switch (AxisUpOrientation)
	{
	case 1: // X up
		switch (AxisForwardOrientation)
		{
		case 1: // X
			// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
			InVector = FVector(InVector.Z, InVector.Y, InVector.X);
			break;
		case 2: // Y
			InVector = FVector(InVector.Y, InVector.Z, InVector.X);
			break;
		case 3: // Z
			InVector = FVector(InVector.Z, InVector.Y, InVector.X);
			break;
		default: // the forward axis is unknown so use the up axis
			InVector = FVector(InVector.Z, InVector.Y, InVector.X);
			break;
		}
		break;

	case 2: // Y up
		{
			switch (AxisForwardOrientation)
			{
			case 1: // X
				InVector = FVector(InVector.X, InVector.Z, InVector.Y);
				break;
			case 2: // Y
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				InVector = FVector(InVector.X, InVector.Z, InVector.Y);
				break;
			case 3: // Z
				InVector = FVector(InVector.Z, InVector.X, InVector.Y);
				break;
			default: // the forward axis is unknown so use the up axis
				InVector = FVector(InVector.X, InVector.Z, InVector.Y);
				break;
			}
			break;
		}
	case 3: // Z up
		{
			switch (AxisForwardOrientation)
			{
			case 1: // X
				InVector = FVector(InVector.X, InVector.Y, InVector.Z);
				break;
				
			case 2: // Y
					
				InVector = FVector(InVector.Y, InVector.X, InVector.Z);
				break;

			case 3: // Z
				// the up and forward axis are the same this isn't possible so assume the forward axis is unknown and use the up axis
				InVector = FVector(InVector.X, InVector.Y, InVector.Z);
				break;
			default:
				InVector = FVector(InVector.Y, InVector.X, InVector.Z);
				break;
			}
			break;
		}
	default:
		break;
	}

	// // if the forward axis is negative then we need to rotate the vector by 180 degrees
	if(AxisForwardSign == -1)
	{
		InVector = FRotator(0.0f, 180.0f, 0.0f).RotateVector(InVector);
	}

	// multiple the in X and Z by the input sign
	InVector.X *= AxisForwardSign;
	InVector.Z *= AxisUpSign;

}

void FAssimpMeshLoaderRunnable::FillDataFromScene(const aiScene* Scene)
{
	if (!Scene || !Scene->HasMeshes())
	{
		return;
	}

	float ScaleFactor = 1.0f;
	if (Scene->mMetaData)
	{
		Scene->mMetaData->Get("UnitScaleFactor", ScaleFactor);
		if (ScaleFactor == 0.0f)
		{
			ScaleFactor = 1.0f;
		}
	}

	int32 AxisUpOrientation = 0;
	int32 AxisUpSign = 0;
	int32 AxisForwardOrientation = 0;
	int32 AxisForwardSign = 0;

	if (Scene->mMetaData)
	{
		Scene->mMetaData->Get("UpAxis", AxisUpOrientation);
		Scene->mMetaData->Get("UpAxisSign", AxisUpSign);
		Scene->mMetaData->Get("FrontAxis", AxisForwardOrientation);
		Scene->mMetaData->Get("FrontAxisSign", AxisForwardSign);
	}

	FRotator Rotation = GetMeshRotation(AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);

	Vertices.Empty();
	Faces.Empty();
	Normals.Empty();

	for (uint32 MIndex = 0; MIndex < Scene->mNumMeshes; ++MIndex)
	{
		const aiMesh* Mesh = Scene->mMeshes[MIndex];
		int32 VertexBase = Vertices.Num();

		for (uint32 NumVertices = 0; NumVertices < Mesh->mNumVertices; ++NumVertices)
		{
			FVector Vertex = FVector(Mesh->mVertices[NumVertices].x * ScaleFactor,
			                         Mesh->mVertices[NumVertices].y * ScaleFactor,
			                         Mesh->mVertices[NumVertices].z * ScaleFactor);
			if (Rotation != FRotator::ZeroRotator)
			{
				TransformMeshMatrix(Vertex, AxisUpOrientation, AxisUpSign, AxisForwardOrientation, AxisForwardSign);
			}
			Vertices.Add(Vertex);

			if (Mesh->HasNormals())
			{
				FVector Normal(
					Mesh->mNormals[NumVertices].x * ScaleFactor,
					Mesh->mNormals[NumVertices].y * ScaleFactor,
					Mesh->mNormals[NumVertices].z * ScaleFactor
				);

				// Apply exactly the same rotation you used on vertices:
				if (!Rotation.IsZero())
				{
					const FQuat Q = Rotation.Quaternion();
					Normal = Q.RotateVector(Normal);
				}
				if (bIsWktExtension)
				{
					Normal *= -1.0f; // WKT normals are inverted
				}

				Normals.Add(Normal.GetSafeNormal());
			}
			else
			{
				Normals.Add(FVector::ZeroVector);
			}
		}

		for (uint32 FaceIndex = 0; FaceIndex < Mesh->mNumFaces; ++FaceIndex)
		{
			const aiFace& Face = Mesh->mFaces[FaceIndex];
			if (Face.mNumIndices == 3)
			{
				Faces.Add(VertexBase + Face.mIndices[0]);
				Faces.Add(VertexBase + Face.mIndices[1]);
				Faces.Add(VertexBase + Face.mIndices[2]);
			}
		}
	}
}
