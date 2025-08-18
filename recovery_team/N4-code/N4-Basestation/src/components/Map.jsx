import {
  MapContainer,
  TileLayer,
  Marker,
  Popup,
  Circle,
  Polyline,
  useMapEvent,
} from "react-leaflet";
import "leaflet/dist/leaflet.css"; // important
import L from "leaflet";

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
      <TileLayer
        attribution='&copy; MapTiler &amp; OpenStreetMap contributors'
        url="http://localhost:8080/styles/basic-preview/{z}/{x}/{y}.png"
      />
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
