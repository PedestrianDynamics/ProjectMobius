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

#include "Subsystems/HeatmapSubsystem.h"

//#include "MobiusWidgetSubsystem.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Kismet/GameplayStatics.h"

UHeatmapSubsystem::UHeatmapSubsystem(): XYSpawnLocation()
{
}

void UHeatmapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// check we are in the game world as we only want to get actors if we are
	if (GetWorld()->IsGameWorld())
	{
		// log the number of heatmaps
		//UE_LOG(LogTemp, Warning, TEXT("Number of Heatmap Actors Added to Heatmap Subsystem: %d"), Heatmaps.Num());
	}
}

void UHeatmapSubsystem::Deinitialize()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(HeatmapGenerationTimerHandle);
    }

    Super::Deinitialize();
}

void UHeatmapSubsystem::UpdateSpawnLocationAndHeatmapSize(const FVector& SpawnOrigin, const FVector& BoundExtents)
{
	{
		// lock the data
		FScopeLock lock(&HeightSpawnDataLock);
		
		// update the spawn location
		XYSpawnLocation = FVector2D(SpawnOrigin.X, SpawnOrigin.Y);

		// update the bounding box extents
		HeatmapBoundingSize = FVector2D(BoundExtents.X, BoundExtents.Y) * 2.0f;

		// create the heatmaps
		//CreateHeatmap(FVector(XYSpawnLocation.X, XYSpawnLocation.Y, 0), BoundExtents);// DEBUG TEST
	}
	// Schedule Heatmap Generation
	ScheduleHeatmapGeneration();
}

void UHeatmapSubsystem::UpdateSpawnHeightLocations(const TArray<float>& NewHeightSpawnLocations)
{
	{
		// lock
		FScopeLock lock(&HeightSpawnDataLock);
		
		// set the new spawn height locations
		HeightSpawnLocations = NewHeightSpawnLocations;

		// ensure the array is ordered from smallest to largest
		HeightSpawnLocations.Sort();
		
	}
	// Schedule Heatmap Generation
	ScheduleHeatmapGeneration();
}

void UHeatmapSubsystem::CreateHeatmap(const FVector& Location, int32 HeatmapIndex)
{
	// Check if the world is valid
	if (GetWorld())
	{
		// check the location has not already been used by another heatmap
		for (AHeatmapPixelTextureVisualizer* Heatmap : Heatmaps)
		{
			if (Heatmap && Heatmap->GetActorLocation() == Location)
			{
				//UE_LOG(LogTemp, Warning, TEXT("Heatmap already exists at this location"));
				//TODO: we just want to update the heatmap if the location is the same and values are different
				return;
			}
		}
		
		// Spawn the heatmap actor at the given location
		AHeatmapPixelTextureVisualizer* HeatmapActor = GetWorld()->SpawnActor<AHeatmapPixelTextureVisualizer>(Location, FRotator::ZeroRotator);

		// log the location of the heatmap
		//UE_LOG(LogTemp, Warning, TEXT("Heatmap Actor Spawned at Location: %s"), *Location.ToString());

		// check if the actor is valid
		if (HeatmapActor)
		{
			// Set the Heatmap Actor Name
			FString HeatmapName = FString::Printf(TEXT("Heatmap_%i"), HeatmapIndex);// TODO: create a floor classification method based on array size

			HeatmapActor->ActorName = HeatmapName;

			// set the floor ID to the index
			HeatmapActor->FloorID = HeatmapIndex;

			// Initialize the heatmap actor
			HeatmapActor->InitializeHeatmap(2, true, HeatmapBoundingSize, 0.0f, true);

			// Add the heatmap actor to the subsystem
			AddHeatmapActor(HeatmapActor);
		}
		else
		{
			//UE_LOG(LogTemp, Warning, TEXT("Failed to spawn heatmap actor"));
		}
	}
	else
	{
		//UE_LOG(LogTemp, Warning, TEXT("World is not valid"));
	}
}

void UHeatmapSubsystem::AddHeatmapActor(AHeatmapPixelTextureVisualizer* HeatmapActor)
{
	// check actor valid
	if(HeatmapActor)
	{
		// add the actor to the array
		Heatmaps.Add(HeatmapActor);
		OnHeatmapAdded.Broadcast(HeatmapActor);

		// log the number of heatmaps
		//UE_LOG(LogTemp, Warning, TEXT("Heatmap Actor Added to Heatmap Subsystem, Number of Heatmaps: %d"), Heatmaps.Num());
	}
	else
	{
		// TODO: this is the only part of this subsystem that relies on the widget subsystem and causes this module to have a circular dependency 
		// UMobiusWidgetSubsystem* ErrorSubsystem = GetWorld()->GetSubsystem<UMobiusWidgetSubsystem>();
		// if(ErrorSubsystem)
		// {
		// 	ErrorSubsystem->DisplayErrorWidget(FText::FromString("Heatmap Subsystem Error"), FText::FromString("Heatmap Actor is invalid, Failed to add it to the Heatmap Subsystem"));
		// }
	}
}

void UHeatmapSubsystem::RemoveHeatmapActor(class AHeatmapPixelTextureVisualizer* HeatmapActor)
{

	Heatmaps.Remove(HeatmapActor);
	
	OnHeatmapRemoved.Broadcast(HeatmapActor);
	
	// destroy the heatmap actor
	HeatmapActor->Destroy();
	
	if(Heatmaps.Num() > 0)
	{
		
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("There are no heatmaps to remove"));
	}
}

void UHeatmapSubsystem::UpdateHeatmaps(const FVector& AgentLocation)
{
	if(Heatmaps.Num() > 0)
	{
		// as the heatmaps are dynamic they can be destroyed at any time so we need to check if they are valid
		// and remove them at the end
		TArray<AHeatmapPixelTextureVisualizer*> HeatmapsToRemove;
		
		// if we have heatmaps then update them
		for(AHeatmapPixelTextureVisualizer* Heatmap : Heatmaps)
		{
			if(Heatmap && !Heatmap->IsHidden())
			{
				Heatmap->UpdateHeatmap(AgentLocation);
			}
			else
			{
				HeatmapsToRemove.Add(Heatmap);
			}
		}

		// once we have updated all the heatmaps we can remove the invalid ones
		for(AHeatmapPixelTextureVisualizer* Heatmap : HeatmapsToRemove)
		{
			Heatmaps.Remove(Heatmap);
			OnHeatmapRemoved.Broadcast(Heatmap);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("There are no heatmaps to update"));
	}
}

void UHeatmapSubsystem::UpdateHeatmapsWithLocations(const TArray<FVector>& LocationArray)
{
	BroadcastTotalAgentCount(LocationArray.Num());

	if (Heatmaps.Num() <= 0)
		return;

	TArray<TArray<FVector>> ValidHeatmapLocations;
	TArray<TArray<FVector>> BetweenValidHeatmapLocations;

	ComputeValidHeatmapLocations(LocationArray, ValidHeatmapLocations, BetweenValidHeatmapLocations);
	BroadcastAgentCounts(ValidHeatmapLocations, BetweenValidHeatmapLocations);
	RunAsyncHeatmapUpdate(LocationArray, ValidHeatmapLocations);
}

void UHeatmapSubsystem::UpdateHeatmapTextureRender()
{
	if(Heatmaps.Num() > 0)
	{
		for(AHeatmapPixelTextureVisualizer* Heatmap : Heatmaps)
		{
			if (Heatmap)
			{
				Heatmap->UpdateHeatmapTextureRender();
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("There are no heatmaps to update"));
	}
}

void UHeatmapSubsystem::ClearEmptyHeatmaps()
{

	// -1 represent the widgets that store the total building stats
	OnUpdateFloorStatCount.Broadcast(-1, 0); // Shouldn't broadcast as this is frequent update
	
	for(AHeatmapPixelTextureVisualizer* Heatmap : Heatmaps)
	{
		if (!Heatmap)
		{
			continue;
		}

		// TODO: this sort of works for now but likely will cause problems later on
		int32 FloorID = Heatmap->FloorID;
		OnUpdateFloorStatCount.Broadcast(FloorID, 0);
		OnUpdateBetweenFloorStatCount.Broadcast(FloorID, 0);
		
		if (Heatmap->bLiveTrackingHeatmap)
		{
			Heatmap->ClearTexture();
			Heatmap->UpdateHeatmapTextureRender();
		}
	}
}

void UHeatmapSubsystem::SaveSelectedHeatmapsToPNG(const TArray<AHeatmapPixelTextureVisualizer*>& HeatmapActorArray)
{
	if (HeatmapActorArray.Num() > 0)
	{
		for (auto HeatmapActor : HeatmapActorArray)
		{
			HeatmapActor->SaveHeatmapToPNG();
		}
	}
}

void UHeatmapSubsystem::SaveSelectedHeatmapsToPNG(const TArray<AHeatmapPixelTextureVisualizer*>& HeatmapActorArray,
                                                  const FString& CurrentTimeString)
{
	if (HeatmapActorArray.Num() > 0)
	{
		for (auto HeatmapActor : HeatmapActorArray)
		{
			HeatmapActor->SaveHeatmapToPNG(CurrentTimeString);
		}
	}
}

void UHeatmapSubsystem::ScheduleHeatmapGeneration()
{
	// Always enqueue ProcessRefresh on GameThread, but only once until it fires
	if (!GetWorld()->GetTimerManager().IsTimerActive(HeatmapGenerationTimerHandle))
	{
		// zero-delay timer means “next tick” on the GameThread
		GetWorld()->GetTimerManager().SetTimer(
			HeatmapGenerationTimerHandle,
			this, 
			&UHeatmapSubsystem::ProcessHeatmapGeneration,
			/*InRate=*/0.1f, // a small delay is required as 0 fires instantly and prevents successful calls
			/*InbLoop=*/false
		);
	}
}

void UHeatmapSubsystem::ProcessHeatmapGeneration()
{
	
	// snapshot inputs under lock
	TArray<float> Heights;
	FVector2D Bounds = FVector2D::ZeroVector;
	FVector2D XY = FVector2D::ZeroVector;
	{
		FScopeLock lock(&HeightSpawnDataLock);
		Heights = HeightSpawnLocations;
		Bounds  = HeatmapBoundingSize;
		XY      = XYSpawnLocation;
	}

	// only rebuild if everything is valid
	if (Heights.Num() == 0 || Bounds.IsZero())
		return;

	// we need to destroy any existing heatmaps
	if (Heatmaps.Num() > 0)
	{
		TArray<AHeatmapPixelTextureVisualizer*> HeatmapsToDestroy = Heatmaps;

		for (AHeatmapPixelTextureVisualizer* Heatmap : HeatmapsToDestroy)
		{
			if (Heatmap)
			{
				RemoveHeatmapActor(Heatmap);
			}
		}
	}

	// Spawn new
	ParallelFor(Heights.Num(), [this, XY, &Heights](int32 Index)
	{
		// (1) Compute the world position off the game thread
		const FVector Pos(XY.X, XY.Y, Heights[Index]);

		// (2) Schedule the actual spawn back on the Game Thread
		AsyncTask(ENamedThreads::GameThread, [this, Pos, Index]()
		{
			CreateHeatmap(Pos, Index);
		});
	});
	
	// clear the timer so future ScheduleRefresh can re-arm
	GetWorld()->GetTimerManager().ClearTimer(HeatmapGenerationTimerHandle);
}

void UHeatmapSubsystem::ComputeValidHeatmapLocations(const TArray<FVector>& LocationArray,
                                                    TArray<TArray<FVector>>& OutValidLocations,
                                                    TArray<TArray<FVector>>& OutBetweenLocations) const
{
        OutValidLocations.Empty();
        OutValidLocations.SetNum(Heatmaps.Num());

        OutBetweenLocations.Empty();
        if (Heatmaps.Num() > 1)
        {
                OutBetweenLocations.SetNum(Heatmaps.Num() - 1);
        }

        for (int32 i = 0; i < Heatmaps.Num(); ++i)
        {
                AHeatmapPixelTextureVisualizer* BottomHeatmap = Heatmaps[i];

                if (!BottomHeatmap)
                        continue;

                if (Heatmaps.IsValidIndex(i + 1))
                {
                        AHeatmapPixelTextureVisualizer* TopHeatmap = Heatmaps[i + 1];

                        for (const FVector& AgentLocation : LocationArray)
                        {
                                if (BottomHeatmap->CheckHeatmapAndLocationValid(AgentLocation))
                                {
                                        OutValidLocations[i].Add(AgentLocation);
                                }
                                else if (AgentLocation.Z > BottomHeatmap->MeshOriginLocation.Z + BottomHeatmap->MaxAddHeight &&
                                         TopHeatmap->MeshOriginLocation.Z > AgentLocation.Z)
                                {
                                        OutBetweenLocations[i].Add(AgentLocation);
                                }
                        }
                }
                else
                {
                        for (const FVector& AgentLocation : LocationArray)
                        {
                                if (BottomHeatmap->CheckHeatmapAndLocationValid(AgentLocation))
                                {
                                        OutValidLocations[i].Add(AgentLocation);
                                }
                        }
                }
        }
}

void UHeatmapSubsystem::BroadcastAgentCounts(const TArray<TArray<FVector>>& ValidLocations,
                                             const TArray<TArray<FVector>>& BetweenLocations) const
{
        for (int32 i = 0; i < BetweenLocations.Num(); ++i)
        {
                OnUpdateBetweenFloorStatCount.Broadcast(i, BetweenLocations[i].Num());
        }

        for (int32 i = 0; i < ValidLocations.Num(); ++i)
        {
                OnUpdateFloorStatCount.Broadcast(i, ValidLocations[i].Num());
        }
}

void UHeatmapSubsystem::RunAsyncHeatmapUpdate(const TArray<FVector>& LocationArray,
                                              const TArray<TArray<FVector>>& ValidLocations)
{
        TWeakObjectPtr<UHeatmapSubsystem> WeakSelf(this);
        Async(EAsyncExecution::Thread, [WeakSelf, LocationArray, ValidLocations]()
              {
                      if (!WeakSelf.IsValid())
                              return;
                      UHeatmapSubsystem* Self = WeakSelf.Get();
                      TRACE_CPUPROFILER_EVENT_SCOPE_STR("Heatmap Subsystem work task");
                      ParallelFor(Self->Heatmaps.Num(), [&](int32 i)
                      {
                              if (Self->Heatmaps[i] && !Self->Heatmaps[i]->IsHidden())
                              {
                                      Self->Heatmaps[i]->UpdateHeatmapWithMultipleAgents(ValidLocations[i]);
                              }
                              else if (Self->Heatmaps[i])
                              {
                                      Self->Heatmaps[i]->UpdateHeatmapAgentCount(LocationArray);
                              }
                      });
              },
              [WeakSelf]()
              {
                      if (!WeakSelf.IsValid())
                              return;
                      UHeatmapSubsystem* Self = WeakSelf.Get();
                      ParallelFor(Self->Heatmaps.Num(), [&](int32 i)
                      {
                              if (Self->Heatmaps[i])
                              {
                                      Self->Heatmaps[i]->UpdateHeatmapTextureRender();
                              }
                      });
              });
}
