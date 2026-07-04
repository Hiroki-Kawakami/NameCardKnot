// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App.tsx";
import { locale } from "./i18n";
import "./index.css";

document.documentElement.lang = locale;

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
