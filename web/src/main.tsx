import { render } from "solid-js/web";
import "./app.css";
import App from "./App";

const target = document.getElementById("app");
if (!target) throw new Error("Missing application mount point");
render(() => <App />, target);
