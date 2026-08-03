# OpenDRIVE prose 規範義務 ('shall') — v1.9.0 章本文抽出

> **分母の第2層＝raw obligation superset**（Annex F 275 checker rules はこの prose 義務をformalize した部分集合）。出典=`thirdparty/opendrive/1.9/spec_html` の本文 'shall' 文（HTMLタグ除去・重複除去、隣接UIDトークンで formalize 判定）。生成=`scratchpad/extract_prose_obligations.py`。
> **★=prose専用（隣接Annex F UID無し＝checker rule未formalize＝追加の非自明ルール候補）**。

- ユニーク 'shall' 義務: **364**（'shall not' 42）
- うち **formalize済み(Annex F UID隣接): 144** / **★prose専用(未formalize): 220**
- 章別: general_architecture 2 / additional_data 3 / coordinate_systems 3 / geometries 12 / roads 41 / lanes 87 / junctions 69 / objects 91 / signals 36 / railroads 20

## 06_general_architecture

### 6.4 header  `06_04_header.html`
- ★ Only speed and priority elements shall be used within the defaultRegulations element.
- ★ The signalRegulations element shall be used for signs that result in different driving rules. signalRegulations elements shall not be used as a signal catalogue.

## 07_additional_data

### 7.0 additional_data  `07_00_additional_data.html`
- ★ These files shall contain ASAM OpenDRIVE elements. include element In ASAM OpenDRIVE, additional files are represented by include elements.
- ★ Upon parsing this element, ASAM OpenDRIVE readers shall immediately start reading the file specified as attribute of the element.
- ★ The parent element under which the include element occurs must be present in both, the parent file and the included file.

## 08_coordinate_systems

### 8.5 geo_referencing  `08_05_geo_referencing.html`
- ★ This data shall be marked as CDATA, because it may contain characters that interfere with the XML syntax of an element’s attribute.
- ★ This data shall be marked as CDATA, because it may contain characters that interfere with the XML syntax of an element’s attribute. offset element In ASAM OpenDRIVE, the offset of a database is represented by the offset element within the header element.
- ★ There shall be no more than one definition of the projection.

## 09_geometries

### 9.2 road_reference_line  `09_02_road_reference_line.html`
- Each road shall have a road reference line.
- ★ There shall be only one road reference line per road.
- The road reference line usually runs in the center of the road but may be laterally offset. geometry elements shall be defined in ascending order along the road reference line according to the s-coordinate.
- One geometry element shall contain only one element that further specifies the geometry of the road.
- ★ If two roads are connected without a junction, the road reference line of a new road shall always begin at the contactPoint element of its successor or predecessor road.
- A road reference line shall have no gaps.
- ★ The s-value of each geometry shall be the sum of all geometry lengths prior Related topics Section 9.3, "Straight line" Section 9.4, "Spiral" Section 9.5, "Arc" Section 10.1, "Introduction to roads" Section 11.1, "Introduction to lanes"

### 9.6 param_poly3  `09_06_param_poly3.html`
- If @pRange="arcLength", p shall be chosen in [0, @length from geometry ].
- If @pRange="normalized", p shall be chosen in [0, 1].
- ★ The parameters @aU, @aV and @bV shall be zero, @bU shall be 0.

### 9.7 poly3  `09_07_poly3.html`
- ★ A curve which cannot be represented by a cubic polynomial w.r.t x parameter Usually, the u/v coordinate system is aligned with the s/t coordinate system at the segment’s start position (@x,@y) and start orientation @hdg, specified in the geometry element.
- ★ An additional shift of the u/v coordinate origin along the v-coordinate axis, while (@x,@y) shall be located at u=0, may be achieved by setting the polynomial parameter @a unequal to zero (see Figure 35 ).

## 10_roads

### 10.1 introduction  `10_01_introduction.html`
- ★ A road shall have at least the center lane.
- ★ A new road element should only start if the properties of the road cannot be described within the previous road element or if a junction is required.d Table 23.
- Roads outside a junction shall not overlap.
- A road shall not overlap with itself.

### 10.2 properties_for_road_sections  `10_02_properties_for_road_sections.html`
- ★ Sections shall be stored in ascending order of s-coordinates.

### 10.3 road_linkage  `10_03_road_linkage.html`
- ★ ASAM OpenDRIVE® ASAM OpenDRIVE v1.9.0 ASAM OpenDRIVE v1.9.0 v1.8.1 1.8.0 ASAM OpenDRIVE Junction guideline 1.0.0 ASAM OpenDRIVE Signal reference 1.0.0 v1.9.0 v1.9.0 v1.8.1 1.8.0 10.3 Road linkage For applications to navigate through a road network, roads must be linked to each other.
- ★ Allowed, prohibited, and recommended road linkage Figure 38 shows cases of prohibited, allowed, and recommended road linkage.
- ★ Overlaps or leaps should be avoided but are not prohibited if the road reference lines are connected properly.
- ★ For junctions, different attribute sets shall be used for the predecessor and successor elements.
- ★ For each, different attribute sets shall be used.
- Shall only be used for elementType "road" elementType e_road_link_elementType required Type of the linked element Rules The following rules apply to road linkage:
- ★ Two roads shall only be linked directly if the linkage is clear.
- ★ If the relationship to successor or predecessor is ambiguous, junctions shall be used.
- For a road as successor or predecessor the @elementType, @elementId and @contactPoint attributes shall be used.
- For a common junction and a direct junction as successor or predecessor the @elementType and @elementId attributes shall be used.
- For a virtual junction as successor or predecessor the @elementType, @elementId, @elementS and @elementDir attributes shall be used. predecessor and/or successor shall be defined at both sides of the road linkage and shall be consistent.

### 10.4 road_type  `10_04_road_type.html`
- ★ When the type of road changes, a new type element shall be created within the parent road element.
- There shall only be ALPHA-2 country codes in use, no ALPHA-3 country codes, because only ALPHA-2 country codes support state identifiers.
- Road type and lane type represent different properties and are both valid if specified. type elements shall be defined in ascending order according to the s-coordinate.
- ★ When the road type changes and a speed limit exists on that road section, a new speed element is required, because road types have no global valid speed limits unless provided by defaultRegulations .
- ★ The speed limit shall be defined for each type element of a road separately.
- ★ Speed limits derived from signals shall always have preference.

### 10.5 elevation  `10_05_elevation.html`
- ★ Elements shall be defined in ascending order along the reference line.
- ★ Roads shall be elevated along their road reference line.
- Road elevation may be defined in combination with superelevation and road shape or standalone. elevation elements shall be defined in ascending order according to the s-coordinate.
- ★ Elements must be defined in ascending order along the reference line.
- When superelevation is defined, it shall apply to the entire road cross section. superelevation elements shall be defined in ascending order according to the s-coordinate.
- ★ Therefore shapes cannot always follow lane offsets.
- ★ Road shape definition Figure 45 shows how the defined t-range must at least cover the maximum t-expansion of the entire road element.
- Shapes may be defined in combination with road elevation. shape elements shall be defined in ascending order, firstly according to the s-coordinate and secondly according to the t-coordinate.
- ★ If only one strip exists, it has no given width and is valid to the end of the lane definition.
- ★ The first coefficients element shall start at the beginning of the road reference line with @s="0".
- A cross section surface shall start and end at the start and end of the road reference line.
- If on a side only one strip is used, it is defined in a strip element with @id="1" or @id="-1" and a width shall not be specified.
- If on a side two strips are specified, a width for the inner strip shall be specified.
- ★ A cross section surface shall not be used in combination with road shape or superelevation.

### 10.6 road_surface  `10_06_road_surface.html`
- ★ When using this method, it must be assured that the geometry of the CRG data matches – within certain tolerance – the geometry of the underlying ASAM OpenDRIVE road.
- Rules The following additional rules apply: @mode=attached shall not be used together with @purpose=friction . @zOffset and @zScale shall not be set for friction values.
- XML example surface CRG file="fancyData.crg" sStart="0.0" sEnd="100.0" orientation="same" mode="attached" tOffset="0.0" / /surface Because CRG data may only cover parts of a road’s surface, it must be made sure that outside the valid CRG area, the elevation information derived from ASAM OpenDRIVE data can still be used.
- ★ If more than one CRG entry is given for the same physical property (attribute purpose) at a given location, then the last entry in the sequence of occurrence in the ASAM OpenDRIVE file shall be the relevant one.
- If a junction element contains a CRG element, none of the connecting roads that belong to this junction shall have a CRG element. @orientation=opposite shall not be used for modes other than @mode=attached and @mode=attached0. @hOffset shall not be used for modes other than @mode=genuine and @mode=global. @sOffset and @tOffset shall not be used with @mode=global.

## 11_lanes

### 11.1 introduction  `11_01_introduction.html`
- ★ Each road shall have at least one lane layer with a center lane.
- The center lane shall have no width, meaning that the width element shall not be used for the center lane.
- The center lane shall have the lane id 0.
- Lane numbering shall start with 1 next to the center lane in positive t-direction in ascending order and -1 next to the center lane in negative t-direction in descending order.
- Lane numbering shall be consecutive without any gaps.
- ★ Lane numbering shall be unique per lane section and layer.
- Each lanes element shall contain at least one laneSection element.
- ★ All laneSection elements shall contain the @s attribute.
- All drivable lanes must be continuous and smooth, with no gaps, and must account for the plan view, profiles, and lane properties during design and implementation.
- ★ The first lane section shall be defined with a value of 0.0 for the @s attribute.
- ★ In older ASAM OpenDRIVE versions a road required at least one lane with a width greater zero.

### 11.2 lane_layers  `11_02_lane_layers.html`
- ★ Each road has exactly one layer of type "permanent" and may have up to one layer of type "temporary".
- ★ For t-coordinates, the sum of all lane widths plus lane offsets on the temporary lane layer shall not be greater than on the permanent lane layer.
- There shall be at least one lane in the "permanent" layer at each s-coordinate of the road.
- Each road shall have exactly one "permanent" and up to one "temporary" lane layer.
- Each lane section in the permanent layer shall have a center lane.
- ★ Lanes in the permanent lane layer shall not use the attribute @length.
- Omitting @layer shall default to @layer="permanent".
- ★ Lanes in the "temporary" layer shall not contain height or material elements.
- ★ For each lane group, the sum of all lane widths and lane offsets on the temporary lane layer shall not be greater than the sum of lane widths and lane offsets on the permanent lane layer.

### 11.3 lane_groups  `11_03_lane_groups.html`
- ★ 1) Contains the center lane, which must be defined for all roads. lane element In ASAM OpenDRIVE, lanes in the center lane group are represented by lane elements within the center element.
- In order to be drivable, each lane section should contain at least one right or left element that is valid for the whole length of that section.
- One center element shall be defined for each s-coordinate.
- For better orientation, lanes should be listed from left to right, that is with descending ID. @direction="reverse" shall not be used to change from right-hand traffic to left-hand traffic and vice versa.

### 11.4 lane_sections  `11_04_lane_sections.html`
- ★ Road section with lane sections Figure 63 shows that every time the number of lanes changes, a new lane section is required.
- ★ The distance between two succeeding lane sections shall not be zero.
- ★ Each lane section shall contain one center element and at least one right or left element.
- Each road shall have at least one lane section. laneSection elements shall be defined in ascending order according to the s-coordinate.
- The length of lane sections shall be greater than zero.
- There shall always be exactly one center lane at each s-position.
- ★ A new lane section shall be defined each time the number of lanes change.
- ★ A lane section without @length shall remain valid until either a new lane section is defined or the road ends.
- A lane section with @length shall remain valid until @length is reached.
- ★ A lane section with @length shall not extend beyond the end of the road.
- ★ A new lane section on the permanent lane layer shall be defined each time lanes on the permanent layer are linked to lanes on the temporary layer.

### 11.5 lane_offset  `11_05_lane_offset.html`
- ★ The absolute position of an offset value is calculated as follows: s = s start + ds where s is the absolute position in the road reference line coordinate system s start is the start position of the element in the reference line coordinate system A new lane offset element is required each time the polynomial function changes.
- Rules The following rules apply to lane offsets: laneOffset elements shall be defined in ascending order according to the s-coordinate.
- A new lane offset shall start when the underlying polynomial function changes.
- ★ There shall be no laneOffset if border definitions are present.

### 11.6 lane_link  `11_06_lane_link.html`
- ★ To allow entering and exiting the lanes on the temporary lane layer at the start and end of that temporary lane layer section, a transition from the permanent to the temporary lane layer or vice versa must be provided for each lane on the temporary lane layer.
- ★ Lane predecessors and successors shall only be used to connect lanes if a physical connection at the beginning or end of both lanes exist.
- ★ Lane that continues across the lane sections shall be connected in both directions.
- ★ Examples where multiple predecessors and successors shall be used:
- Example where multiple predecessors and successors shall not be used:
- ★ If a new lane appears besides, only the continuing lane shall be connected to the original lane, not the appearing lane.
- ★ Two lanes shall only be linked if their linkage is clear.
- If the relationship to a predecessor or successor is ambiguous, junctions shall be used.
- ★ Multiple predecessors and successors shall be used if a lane is split abruptly or several lanes are merged abruptly.
- All lanes that are connected shall have a non-zero width at the connection point.
- Lanes that have a width of zero at the beginning of the lane section shall have no predecessor element.
- Lanes that have a width of zero at the end of the lane section shall have no successor element.
- The link element shall be omitted if the lane starts or ends in a junction or has no link.
- At the start and at the end of a temporary lane layer section, all drivable lanes with non-zero width on the temporary lane layer shall be linked to lanes on the permanent lane layer.

### 11.7 lane_geometry  `11_07_lane_geometry.html`
- ★ Lane geometries shall be defined relative to the start of the corresponding lane section.
- A specific lane geometry shall remain valid until another lane geometry of that type is defined or the lane section ends.
- ★ Lane geometries of identical types shall be defined in ascending order.
- ★ If both width and lane border elements are present for a lane section in the ASAM OpenDRIVE file, the application must use the information from the width elements.
- ★ The width of the lane shall be defined for the full length of the lane section.
- This means that there must be a width element for @s="0".
- ★ The width of a lane shall remain valid until a new width element is defined or the lane section ends.
- A new width element shall be defined when the variables of the polynomial function change. width elements shall not be used together with border elements in the same lane group. width elements shall be defined in ascending order according to the s-coordinate.
- ★ Width (ds) shall be greater than or equal to zero.
- ★ If both width and lane border elements are present for a lane section in the ASAM OpenDRIVE file, the application shall use the information from the width elements.
- A new border element shall be defined when the variables of the polynomial function change. border elements shall be defined in ascending order according to the s-coordinate.
- ★ Lane borders shall not intersect inner lanes.
- ★ Related topics Section 11.3, "Lane groups" Section 11.4, "Lane sections" 11.7.3 Lane height Lane height shall be defined along the h-coordinate.
- To modify the lane height, for example for curbstones, the height element shall be used. height elements shall be defined in ascending order according to the s-coordinate.
- ★ The center lane shall not be elevated by lane height.
- ★ Lane height shall not be used to define road elevation or superelevation.
- ★ Lane height shall be used for small scale elevation only.
- ★ If a lane has @level="true", then all further outward lanes shall be lanes with @level="true" until the edge of the road is reached.

### 11.8 lane_properties  `11_08_lane_properties.html`
- ★ If multiple elements are defined, they must be listed in ascending order.
- ★ The center lane shall have no material elements.
- The material elements of a lane shall remain valid until another material element starts or the lane section ends. material elements shall be defined in ascending order according to the s-coordinate Related topics Section 11.1, "Introduction to lanes" Section 11.3, "Lane groups" Section 11.4, "Lane sections" 11.8.3 Lane speed limit The maximum speed allowed on a lane may be defined.
- ★ The center lane shall have no speed limit.
- The speed limit of a lane shall remain valid until another speed limit is defined or the lane section ends. speed elements shall be defined in ascending order according to the s-coordinate.
- ★ If multiple elements are defined, they shall be listed in ascending order.
- ★ The center lane shall have no access rules.
- The access rules of a lane shall remain valid until another access rule is defined or the lane section ends. access elements shall be defined in ascending order according to the s-coordinate.
- ★ At a given s-position, either only deny or only allow values shall be given.
- ★ For a new s-position, all restrictions must be defined again, even if only a subset changes.

### 11.9 lane_road_markings  `11_09_lane_road_markings.html`
- Rules The following rules apply to road markings: roadMark elements shall only be used to describe the outer lane marking. roadMark elements shall be defined in ascending order according to the s-coordinate.
- ★ The center line of the lane marking shall be positioned on the lane’s outer border line in such a way that the outer half of the lane marking is physically placed on the next lane.
- ★ The roadMark elements of a lane shall remain valid until another roadMark element starts or the lane section ends.
- ★ 0..1) Each type definition shall contain one or more line definitions with additional information about the lines that the road marking is composed of.
- ★ 0..1) Irregular road markings that cannot be described by repetitive line patterns may be described by individual road marking elements.

### 11.10 specific_lane_rules  `11_10_specific_lane_rules.html`
- Applications may have specific lane rules that are only valid in the respective application, but not in ASAM OpenDRIVE. rule elements shall be defined in ascending order according to the s-coordinate.

## 12_junctions

### 12.1 introduction  `12_01_introduction.html`
- ★ Direct junctions are junctions where traffic can change roads but cannot cross other traffic.
- ★ Crossings are junctions where traffic cannot change the roads.
- No junctions of any type shall overlap each other.
- The connection element of a junction of @type="direct" shall not have the @connectingRoad attribute.
- ★ The connection element of a junction of @type="default" or @type="virtual" shall not have the @linkedRoad attribute.

### 12.2 common_junctions  `12_02_common_junctions.html`
- ★ Junctions shall only be used when roads cannot be linked directly.
- The @mainRoad, @orientation, @sStart and @sEnd attributes shall only be specified for virtual junctions.
- ★ The @overlapZone attribute shall only be specified for direct junctions.

### 12.3 incoming_roads  `12_03_incoming_roads.html`
- ★ Connecting roads shall not be incoming roads.

### 12.4 connecting_roads  `12_04_connecting_roads.html`
- ★ There shall only be one connection for a specific combination of @incomingRoad and @connectingRoad.
- ★ For each connection , its laneLink elements shall only be specified for the lanes that lead into the junction.
- The linked lanes shall fit smoothly as described for roads (see Section 10.3, "Road linkage" ).
- ★ The @connectingRoad attribute shall not be used for junctions with @type="direct".
- ★ It is only required if priorities cannot be derived from signs or signals in a junction or on tracks leading to a junction.
- Attributes of the priority element Name Type Use Description high string required ID of the prioritized road low string required ID of the road with lower priority Rules The following rules apply to priorities of roads within a junction: priority elements should only be used if there are no signals defined. priority elements shall be defined with a pair of one @high and one @low attribute.
- The value start shall be used to indicate that the connecting road runs along the linkage indicated in the laneLink element.
- ★ The value end shall be used to indicate that the connecting road runs along the opposite direction of the linkage indicated in the laneLink element Related topics Section 10.4, "Road type"

### 12.5 cross_paths  `12_05_cross_paths.html`
- Cross paths shall be within the area of a common junction or a virtual junction.
- Start and end of the crossing road shall reach the linked lanes specified by the startLaneLink and endLaneLink elements.
- The start and end points of the crossing road and its lanes shall be fully contained within the linked lanes specified by the startLaneLink and endLaneLink elements.
- Cross paths shall only connect lanes with @type="walking" or @type="biking".
- ★ The @junction attribute shall contain the id of the junction to which a road belongs.

### 12.6 direct_junctions  `12_06_direct_junctions.html`
- ★ May be chosen freely. type e_junction_type required Direct junctions must be of type "direct". connection element In ASAM OpenDRIVE, connections in direct junctions are represented by connection elements within the junction element.
- Direct junctions shall connect one road on one side with multiple roads on the other side.
- Direct junctions shall only be used for splitting or merging roads without crossing traffic.
- The @linkedRoad attribute shall only be used for junctions with @type="direct".
- The junction shall be placed where the headings of road, ramp, or slip lane are identical.
- Only one pair of laneLink elements shall have @overlapZone attributes to define the overlapping lanes.
- The value of the @overlapZone attribute shall cover at least the overlapping area, but may be larger.
- ★ Direct junctions cannot be used if multiple lanes overlap.
- ★ In this case common junctions shall be used (see Section 12.2, "Common junctions" ).
- ★ Direct junctions cannot be used if traffic crosses.

### 12.7 virtual_junctions  `12_07_virtual_junctions.html`
- ★ This attribute is mandatory for virtual junctions and shall not be specified for other junction types. name string optional Name of the junction.
- ★ This attribute is mandatory for virtual junctions and shall not be specified for other junction types.
- ★ This attribute is mandatory for virtual junctions. type e_junction_type required Virtual junctions must be of type "virtual". connection type="default" element In ASAM OpenDRIVE, the connections are represented by connection elements with the value default in the @type attribute within the junction element.
- Virtual junctions shall not replace common junctions and crossings that connect multiple roads.
- ★ Virtual junctions shall be used for branches off the main road only.
- ★ Virtual junctions shall not have controllers and therefore no traffic lights.
- All connecting roads within the virtual junction shall either start or end at @sStart or at @sEnd.
- There shall only be one @sStart and one @sEnd attribute for the virtual junction.
- The heading of the connecting roads and the @mainRoad shall be equal at @sStart and at @sEnd.
- ★ The linked lanes shall fit smoothly (see Section 10.3, "Road linkage" ).
- ★ The @mainRoad, @sStart, @sEnd, @orientation attributes shall only be valid for junctions of type virtual.
- The crossing road shall not exceed the values for s and t of the main road defined by the @roadAtStart and @roadAtEnd attributes.
- Virtual connections shall not replace regular geometrical elements described by road linkage and lane linkage.
- ★ Virtual connections shall only be defined in virtual junctions.

### 12.8 crossings  `12_08_crossings.html`
- ★ ASAM OpenDRIVE® ASAM OpenDRIVE v1.9.0 ASAM OpenDRIVE v1.9.0 v1.8.1 1.8.0 ASAM OpenDRIVE Junction guideline 1.0.0 ASAM OpenDRIVE Signal reference 1.0.0 v1.9.0 v1.9.0 v1.8.1 1.8.0 12.8 Crossings Crossings are junctions where traffic of two or more different roads crosses at the same level, but the traffic cannot change the roads at crossings.
- ★ May be chosen freely. type e_junction_type required Crossings must be of type "crossing". roadSection element In ASAM OpenDRIVE, the ranges with possible crossing traffic at crossings are represented by roadSection elements within the junction element.
- Junctions with @type="crossing" shall only have roadSection elements.
- Only one road defined by the @roadId attributes of the roadSection elements shall have high priority.
- ★ The values for the @sStart and @sEnd attributes of the roadSection elements shall at least cover the area where the roads overlap.

### 12.9 junction_reference_line  `12_09_junction_reference_line.html`
- ★ Junction reference lines shall be defined by one geometry element.
- This geometry element shall have only one line element.
- The geometry element of a junction reference line shall be defined in a way that every point of the junction can be reached with a perpendicular straight line.
- ★ If a junction boundary is specified, a junction reference line shall cross the junction boundary or be at least tangent to the junction boundary at one point.

### 12.10 junction_boundary  `12_10_junction_boundary.html`
- Segments shall be ordered counter clockwise.
- Segments shall be defined to reach the start or end of all roads connected to the junction.
- Segments shall close the entire junction boundary.
- ★ If the existing roads are not sufficient to define a closed junction boundary, additional roads shall be defined for the missing segments.
- ★ These additional roads shall follow the rules of road linkage to the incoming roads, outgoing roads, or connecting roads (see Section 10.3, "Road linkage" ), but are not required to have any lanes or connections.
- ★ Start and end of an additional road shall point away from both incoming and/or outgoing roads to the inside of the junction.
- ★ The @junction attribute shall contain the id of the junction to which the road belongs.

### 12.11 junction_elevation_grid  `12_11_junction_elevation_grid.html`
- A junction shall have only one elevation grid.
- If a junction boundary is defined, the elevation grid shall be valid for the area enclosed by the junction boundary.
- ★ The elevation grid shall be defined with vectors perpendicular to the junction reference line.
- ★ The elevation grid shall be valid from the point where a traffic participant enters the junction boundary until it leaves the junction boundary on an outgoing road.
- ★ If an elevation grid is present, it shall override any elevation values derived from any roads that are part of the junction or its boundary.
- ★ The coefficients \(c\) and \(d\) of the polynoms shall be 0 if there are not enough support points in the elevation grid to calculate them.

### 12.12 junction_crg_surface  `12_12_junction_crg_surface.html`
- ★ Junction CRG surfaces shall use the junction reference line.

## 13_objects

### 13.1 introduction  `13_01_introduction.html`
- ★ Conversely, each surface of the bounding volume should intersect with at least one point of the object.
- ★ However, every point of an outline or skeleton element of an object must be contained in its bounding volume.
- ★ Dynamic object cannot change its position. hdg double optional rad Heading angle of the object relative to road direction height t_grEqZero optional m Height of the object s bounding box. @height is defined in the local coordinate system u/v along the z-axis id string required Unique ID within database invalidated boolean optional 1.9.0 Indicates whether the object is currently invalidated.
- ★ The type of an object shall be given by the @type attribute.
- ★ An object may either be dynamic or static, but an object cannot change its position or its heading, pitch, or roll.
- Objects derived from ASAM OpenSCENARIO shall not be mixed with objects described in ASAM OpenDRIVE.
- The direction for which objects are valid shall be specified.
- The origin position of the object shall be described with s- and t-coordinates along the road surface.
- ★ Omitting @temporary shall default to @temporary="false".
- ★ Omitting @invalidated shall default to @invalidated="false".

### 13.2 object_outline  `13_02_object_outline.html`
- ★ An object with more than one outline element must have exactly one outer outline.
- ★ All inner outlines must be located fully inside the outer outline.
- ★ A driving simulation application might conclude that a vehicle cannot pass the tree if it only just recognized a bounding volume representing the tree.
- ★ An element shall be followed by two or more cornerRoad elements, by two or more cornerLocal elements, or by one or more curveLocal elements.
- ★ ASAM OpenDRIVE 1.4 outline definitions (without outlines parent element) shall still be supported.
- ★ Must be unique within one object. laneType e_laneType optional Describes the lane type of the outline outer t_bool optional Defines if outline is an outer outline of the object.
- ★ An outline element shall be followed by two or more cornerRoad elements, by two or more cornerLocal elements, or by one or more curveLocal elements.
- It may be specified if the described outline is located at the outer border of the object. outlines elements shall have exactly one outline element with @outer=true.
- ★ Omitting @outer shall default to @outer=true.
- ★ All points of the outline element must be located inside the bounding volume.
- Outlines with @closed=true shall connect the last point in the outline to the first as if it was the next point in the sequence
- ★ If an inner outline touches the outer outline, the reference point shall be identical in both outlines.
- ★ Must be unique within one outline s t_grEqZero required m s-coordinate of the corner t double required m t-coordinate of the corner XML example Ex_TrafficIsland-CornerRoad.xodr Calculation The outline \(\mathbf{e}\) between two given neighboring cornerRoad elements i and i+1 is calculated with the following linear function with respect to a common normalized interpolation parameter \(p\).
- There shall be at least two cornerRoad elements inside an outline element.
- There shall be no mixture of cornerRoad , cornerLocal , and curveLocal elements inside the same outline element.
- The @id attribute of a cornerRoad element shall be mandatory when the parent also has a markings element.
- There shall be at least two cornerLocal elements inside an outline element.
- The @id attribute of a cornerLocal element shall be mandatory when the parent also has a markings element.
- ★ Corner local curve helps to model objects with a smooth geometry such as traffic islands or additional road markings which cannot be modeled as lane boundaries.
- ★ Shall be unique within one outline. length t_grZero optional m Length of the element s reference line u double required m Local u-coordinate of the corner v double required m Local v-coordinate of the corner z double required m Local z-coordinate of the corner line element In ASAM OpenDRIVE, a straight line is represented by the line element within the curveLocal element.
- There shall be at least one curveLocal element inside an outline element.
- ★ Outlines defined by curveLocal elements shall be continuous.
- If no @length is provided, the curve shall extend up to the next curveLocal element.
- For paramPoly3 elements with @pRange="arcLength", p shall be chosen in [0, @length from curveLocal ].
- ★ For paramPoly3 elements with @pRange="normalized", p shall be chosen in [0, 1].

### 13.3 object_skeleton  `13_03_object_skeleton.html`
- ★ Only objects of certain values of the @type attribute shall be described with these skeleton polylines.
- ★ An polyline element shall be followed by either two or more vertexRoad elements or by two or more vertexLocal elements.
- ★ Must be unique within one object.
- ★ A polyline element shall be followed by either two or more vertexRoad elements or by two or more vertexLocal elements.
- All points of the polyline element must be located inside the bounding volume.
- ★ The boundary, defined by either width and height or radius, of each point of an object s skeleton shall at least partially be located inside the bounding volume.
- Changes in @radius or @width and @height attributes between points of the polyline element shall be interpolated linearly.
- ★ Each polyline element shall either use @radius or @width and @length attributes for all of its vertex elements.
- ★ Each vertexRoad element must lie inside the object s bounding volume.
- ★ Must be unique within one polyline. intersectionPoint t_bool optional 1.8.0 Vertex point is intersecting the ground.
- There shall be at least two vertexRoad elements inside a polyline element.
- There shall be no vertexLocal element next to a vertexRoad element inside the same polyline element. vertexRoad elements shall not use @radius together with @width and @length attributes in one polyline element.
- ★ Values of @radius or @width and @length attributes shall be interpolated linearly between two vertexRoad points.
- ★ Each vertexLocal element must lie inside the object s bounding volume.
- There shall be at least two vertexLocal elements inside an polyline element.
- There shall be no vertexRoad element next to a vertexLocal element inside the same polyline element. vertexLocal elements shall not use @radius together with @width and @length attributes in one polyline element.

### 13.4 repeating_objects  `13_04_repeating_objects.html`
- ★ The t-coordinate for each s-coordinate between tStart and tEnd is interpolated using a parametric cubic polynomial based on @tStart, @bT, @cT, and @dT if at least one of the coefficients bT, cT, and dT are provided using a linear interpolation based on @tStart and @tEnd if none of the coefficients are provided Within each section, the distance attribute defines how the object is repeated:
- ★ The origin of each instance placed in a section of repetition must be inside that section (including its border).
- ★ The dimensions of each instance (e.g. its bounding volume) may extend beyond that section on either end and shall not be cut off at any of the borders of the section.
- ★ Attributes of the repeated object shall overrule the attributes from the original object.
- Applicable parameters of the repeated object shall overrule the parameters from the original object.
- Repeated objects shall have valid s-coordinates and lengths.
- Repeated objects with an outline shall use cornerLocal @widthStart and @widthEnd shall not be applicable for objects where @radius is set. @lengthStart, @lengthEnd, @widthStart, @widthEnd, @heightStart, and @heightEnd shall not be applicable for objects with an outlines , an outline , or a skeleton element.
- ★ If t is defined by a cubic polynomial, tEnd must be equal to t(length).
- ★ Parameters @bT, @cT, and @dT shall be considered equal to 0.0 if not explicitly provided.

### 13.6 lane_validity_obj  `13_06_lane_validity_obj.html`
- The range given by all validity elements shall be a subset of the parent s @orientation attribute:
- ★ For right-hand traffic, @orientation="+" implies that the validity element shall only span negative lane ids, while @orientation="-" implies that the validity element shall only span positive lane ids.
- If the given validity elements span both, positive and negative lane ids, @orientation="none" shall be used.
- ★ For left-hand-traffic, @orientation="-" implies that the validity element shall only span negative lane ids, while @orientation="+" implies that the validity element shall only span positive lane ids.
- ★ The value of the @fromLane attribute shall be lower than or equal to the value of the @toLane attribute.

### 13.8 object_markings  `13_08_object_markings.html`
- The color of the marking shall be defined.
- If no outline is used, the markings element shall be inside the object element.
- If no outline is used, the cornerReference element cannot be used.
- If an outline is used, any markings element shall be inside an outline element.
- The marking of an object with an outlines element shall either completely or partially be defined on one of its outlines.
- To specify a marking that fully encloses an object on a closed outline, the marking shall have two cornerReference elements with the same @id.
- For marking elements with cornerReference elements that are not directly subsequent on the outline, all points in between shall be included as well. cornerReference elements shall use the same order of @id attributes as the points of the outline they belong to.
- ★ If an outline is used, at least two cornerRoad elements or at least two cornerLocal elements or at least one curveLocal element shall be referenced via cornerReference elements.

### 13.9 object_borders  `13_09_object_borders.html`
- ★ If @useCompleteOutline is true, the cornerReference element shall not be defined.
- ★ All outline elements of an outlines element shall have different @outlineId values.

### 13.11 tunnels  `13_11_tunnels.html`
- The @type of the tunnel shall be specified.

### 13.12 bridges  `13_12_bridges.html`
- The @type of the bridges shall be specified.

### 13.13 object_surface  `13_13_object_surface.html`
- ★ Because CRG data may only cover parts of a road s surface, it must be made sure that the elevation information derived from ASAM OpenDRIVE data can still be used outside of the valid CRG area.
- ★ In this case, the height of the road at that position shall be identical to the height defined in the rest of this standard, as if the object CRG were not present.
- ★ Circular objects or objects with outlines elements shall not contain surface elements.
- Outside the bounding volume, CRG data from the object shall be ignored.
- The bounding volumes of objects with surface elements shall not overlap.
- ★ The local coordinate system of the CRG shall be identical to the local coordinate system of the object to which it belongs.
- The reference line, inertial position, curvature, and heading of the CRG file shall be ignored.
- An object with a surface element shall be referenced on all roads it overlaps, using object and objectReference elements.
- An object shall not reference more than one CRG file.
- The @height and @zOffset attributes of an object with a surface element shall be ignored when calculating the surface elevation.
- If a road surface CRG is present, that is, the CRG area overlaps the bounding volume of the object and has any mode other than attached, then @hideRoadSurfaceCRG shall be false.
- ★ If crgEvaluv2z returns NaN, then the road height at that position shall be the ASAM OpenDRIVE height in addition to the road surface CRG, if it is present.
- ★ The value of @hideRoadSurfaceCRG attribute shall have no influence.The value of @hideRoadSurfaceCRG attribute shall have no influence.

### 13.14 object_examples  `13_14_object_examples.html`
- ★ ASAM OpenDRIVE® ASAM OpenDRIVE v1.9.0 ASAM OpenDRIVE v1.9.0 v1.8.1 1.8.0 ASAM OpenDRIVE Junction guideline 1.0.0 ASAM OpenDRIVE Signal reference 1.0.0 v1.9.0 v1.9.0 v1.8.1 1.8.0 13.14 Combinations of elements and attributes for object types 13.14.1 barrier A barrier is a continuous object, which cannot be passed.

## 14_signals

### 14.1 introduction  `14_01_introduction.html`
- ★ Signals shall be placed in relation to a specific road.
- ★ Signals shall be positioned in such a way that it is clear to which road or lane they belong and where their validity starts.
- ★ Ambiguity about their interpretation shall be avoided.
- Signals shall have a specific type and subtype.
- If present, signals shall be used in priority to other traffic rules.
- ★ A country code shall be added to refer to country-specific rules using the @country attribute.
- ★ Signals without @invalidated shall default to @invalidated=false.
- ★ Omitting @invalidated shall default to @temporary="false".

### 14.5 multiple_roads  `14_05_multiple_roads.html`
- ★ The signalReference element shall include the longitudinal, @s attribute, and lateral, @t attribute, position on the road where the referenced signal should take effect.
- ★ The signalReference element shall also include an @orientation attribute to specify whether the referenced signal applies to traffic flowing in the positive, negative, or both s-directions.
- Signal reference shall be used for signals only.
- The direction on the road for which the referenced signal is valid shall be specified for every signalReference element using the @orientation attribute.
- The range given by all validity elements shall be a subset of the parent s @orientation attribute: include::partial$rules/road/signal/reference/right_hand_traffic_lane_ids.adoc[].

### 14.6 controllers  `14_06_controllers.html`
- ★ Controllers shall be valid for one or more signals.

### 14.7 signal_boards  `14_07_signal_boards.html`
- ★ Static signal boards shall be specified to be @type="staticBoard".
- ★ Static signal boards shall be specified to be @dynamic="false".
- ★ The validity element of a sign element shall override the validity element of the parent signal element.
- The signalDependency element of a sign element shall override the signalDependency element of the parent signal element.
- ★ Static boards shall not be used for single signals, for example, a stop sign on a single sheet of metal.
- ★ Variable message boards shall be specified to be @dynamic="true".
- ★ The validity element of a displayArea element shall override the validity element of the parent signal element.
- ★ The signalDependency of a displayArea element shall override the signalDependency element of the parent signal element.
- A multi board shall have at least one static signal board and at least one variable message board.
- Multi boards shall be specified to be @type="multiBoard".
- ★ Multi boards shall be specified to be @dynamic="true".
- ★ Therefore variable message boards that are on the same gantry shall be grouped and their indexes shall be redefined if not unique.
- Each gantry shall have one vmsGroup element with at least one vmsBoardReference element.
- ★ All variable message boards within a vmsGroup element shall belong to the same gantry.

### 14.8 signal_semantics  `14_08_signal_semantics.html`
- ★ Functionality for signal semantics cannot be used for visualization.
- ★ Stop sign priority recommend for default junction priority recommended prohibited Specifies that certain types of traffic participants are not allowed to enter.
- ★ Attributes of the priority element Name Type Use Introduced type e_signals_semantics_priority required 1.8.0 prohibited element UML class: t_signals_semantics_prohibited XML tag: prohibited (Multiplicity:
- ★ Signal semantics shall not be specified for signs if no category for the desired traffic behavior exists.
- ★ Related topics Section 6.4.3, " defaultRegulations element" Section 14.8.2, Traffic participants Annex E, Categories for signal semantics (informative) 14.8.2 Traffic participants Signals semantics using prohibited , supplementaryAllows , or supplementaryProhibits can specify traffic participants.
- ★ These define particular types of traffic participants the signal either explicitly applies to (using prohibited or supplementaryProhibits ) or which are exempt from it (using supplementaryAllows ).
- ★ In OpenDRIVE 1.9.0, the type of animal cannot be specified.
- ★ Rules The enum literal "other" shall only be used if no other option fits.

## 15_railroads

### 15.1 introduction  `15_01_introduction.html`
- ★ ASAM OpenDRIVE cannot be used for complex railway networks and railway signals.
- ★ 0..1) Container for all railroad definitions that shall be applied along a road.
- ★ All other entries shall be covered with the existing elements, for example, track definition by road , signal definition by signal , etc.

### 15.2 railroad_tracks  `15_02_railroad_tracks.html`
- ★ They cannot share a road with other traffic elements.
- The road reference line shall be in the center of the pair of railroad tracks.
- There shall only be one tram or one rail lane per road.
- ★ The width of the lane shall be at least the width rail-bound vehicles.

### 15.3 switches  `15_03_switches.html`
- ★ Static switches cannot be changed during the simulation.
- Must be consistent with parent containing this railroad element. s t_grEqZero required m s-coordinate of the switch, that is, the point where main track and side track meet Rules The following rules apply to main tracks:
- ★ Main tracks shall not be used to connect two switches.
- ★ Side tracks shall be used to link two switches only.
- Partner switches shall be used to indicate that a side track links two switches.

### 15.4 stations  `15_04_stations.html`
- ★ Each station shall have at least one platform, which may be further divided into segments.
- ★ A station element shall be followed by at least one platform element.
- ★ Related topics Section 15.1, "Introduction to railroads" Section 15.4.1, Platforms Section 15.4.2, Segments 15.4.1 Platforms A station shall contain at least one platform.
- ★ A platform shall be referenced by one or more railroad tracks.
- There shall be at least one platform per station.
- ★ A platform shall contain at least one segment.
- ★ The segment element must be specified.
- ★ There shall be at least one segment per platform.

