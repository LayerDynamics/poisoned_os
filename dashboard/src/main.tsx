import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./app/App";
import "./style.css";

createRoot(document.getElementById("root")!).render(<StrictMode><App /></StrictMode>);
if ("serviceWorker" in navigator) void navigator.serviceWorker.register("/sw.js");
