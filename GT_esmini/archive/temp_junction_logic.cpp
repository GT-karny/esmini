
    int AutoLightController::DetectJunctionTurn(double lookahead)
    {
        if (!vehicle_) return 0;

        roadmanager::RoadProbeInfo info;
        // Use LOOKAHEADMODE_AT_LANE_CENTER to follow the lane shape/route
        auto ret = vehicle_->pos_.GetProbeInfo(lookahead, &info, roadmanager::Position::LookAheadMode::LOOKAHEADMODE_AT_LANE_CENTER);

        if (ret != roadmanager::Position::ReturnCode::OK)
        {
             // Try slightly shorter if end of road? No, Keep simple.
             return 0;
        }

        // Check if the probe point is in a junction (or if we are heading into one)
        // Note: GetProbeInfo returns info *at* the target. 
        // If target is in junction, junctionId != -1
        if (info.road_lane_info.junctionId == -1)
        {
            return 0; // Not a junction
        }

        // Check heading difference relative to current vehicle heading
        // info.relative_h is the heading of the road at probe point relative to vehicle.
        // We need to guard against wrapping (should be handled by GetProbeInfo but good to allow range)
        double relH = info.relative_h; 
        
        // esmini (OpenDRIVE) Heading: Counter-Clockwise is positive.
        // Left turn -> Heading increases -> relH > 0
        // Right turn -> Heading decreases -> relH < 0
        
        if (relH > JUNCTION_TURN_THRESHOLD)
        {
            return 1; // Left
        }
        else if (relH < -JUNCTION_TURN_THRESHOLD)
        {
            return -1; // Right
        }

        return 0;
    }
