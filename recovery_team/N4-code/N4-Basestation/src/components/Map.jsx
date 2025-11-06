import React, { useEffect } from "react"; // ⬅️ Add useEffect
import {
  MapContainer,
  Marker,
  Popup,
  Polyline,
  useMap, // ⬅️ Add useMap hook
  useMapEvent,
} from "react-leaflet";
import "leaflet/dist/leaflet.css";
import L from "leaflet";
// Import the VectorGrid plugin. Make sure 'leaflet.vectorgrid' is installed and imported in your entry file if needed.
import "leaflet.vectorgrid"; 

// 🛑 IMPORTANT: This component replaces the standard <TileLayer>
// It uses L.vectorGrid.protobuf to fetch and render PBF vector tiles.
function VectorTileLayer() {
  const map = useMap(); // Access the Leaflet map instance

  useEffect(() => {
    // 1. Define the URL for your vector tiles
    const vectorTileUrl = "http://localhost:8080/data/kenya-osm/{z}/{x}/{y}.pbf";

    // 2. Define the styles for the vector features (lines, polygons, etc.)
    // You MUST define these styles or the map will appear blank.
    const vectorStyles = {
        // You'll need to confirm the actual layer names in your OSM MBTiles
        // metadata (e.g., 'water', 'landuse', 'roads'). 'boundary' is common.
        // 'default' is a fallback, but specific layers are better.
        'boundary': { // Example layer name
            weight: 2,
            color: 'red',
            opacity: 1,
            fill: false,
            // Use this function to style based on properties, e.g., properties.admin_level
            // style: function (properties, zoom) { ... return {color: 'blue'} }
        },
        'water': {
            weight: 0,
            fillColor: '#80c4ff',
            fillOpacity: 0.6,
            fill: true
        },
        'roads': {
            weight: 1,
            color: '#000000',
            opacity: 0.8,
            fill: false
        },
        // Fallback style for all other unlisted vector layers
        'default': { 
          weight: 1,
          color: '#555555',
          opacity: 0.5,
          fill: false
        }
    };
    
    // 3. Create and add the VectorGrid layer
    const vectorGrid = L.vectorGrid.protobuf(vectorTileUrl, {
      vectorTileLayerStyles: vectorStyles,
      attribution: '&copy; N4 OSM Data | Leaflet.VectorGrid',
      rendererFactory: L.canvas.tile, // Use Canvas for better performance
      maxNativeZoom: 14 // Adjust this based on your MBTiles max zoom
    });

    vectorGrid.addTo(map);

    // Cleanup function: remove the layer when the component unmounts
    return () => {
      map.removeLayer(vectorGrid);
    };
  }, [map]); // Dependency array: run only when the map object changes

  return null;
}

// Your existing component to handle map clicks/positioning
function CurrentPosition({ position }) {
  const map = useMapEvent("click", () => {
    map.setView(position, map.getZoom());
  });
  return position === null ? null : (
    <Marker position={position}>
      <Popup>Here I am!</Popup>
    </Marker>
  );
}


function Map({ position, path }) {
  // Draw the path as a polyline, show start/end markers
  const start = path && path.length > 0 ? path[0] : null;
  const end = path && path.length > 0 ? path[path.length - 1] : null;

  return (
    <MapContainer
      center={position}
      zoom={13}
      scrollWheelZoom={true}
      style={{ height: "100%", width: "100%" }}
    >
      
      {/* 🛑 Use the custom VectorTileLayer component instead of TileLayer */}
      <VectorTileLayer /> 

      {/* Your existing elements for flight path and markers */}
      {path && path.length > 1 && (
        <Polyline positions={path} color="red" />
      )}
      {start && (
        <Marker position={start}>
          <Popup>We are here</Popup>
        </Marker>
      )}
      {end && (
        <Marker position={end}>
          <Popup>Rocket location</Popup>
        </Marker>
      )}
      <CurrentPosition position={position} />
    </MapContainer>
  );
}

export default Map;