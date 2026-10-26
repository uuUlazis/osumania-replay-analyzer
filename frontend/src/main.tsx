import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import "./styles/global.css";

interface BoundaryState {
  error: Error | null;
}

// Last-resort boundary: a render crash shows the error instead of a
// silent blank window (the webview keeps the dark background either way).
class ErrorBoundary extends React.Component<
  { children: React.ReactNode },
  BoundaryState
> {
  state: BoundaryState = { error: null };

  static getDerivedStateFromError(error: Error): BoundaryState {
    return { error };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo) {
    console.error("render crash:", error, info.componentStack);
  }

  render() {
    if (this.state.error) {
      return (
        <div
          style={{
            padding: 24,
            color: "#e34a4a",
            background: "#14151a",
            minHeight: "100vh",
            fontFamily: "monospace",
            whiteSpace: "pre-wrap",
          }}
        >
          <h2 style={{ marginBottom: 12 }}>界面出错了</h2>
          <div>{String(this.state.error.message || this.state.error)}</div>
          <pre style={{ color: "#8a8fa3", fontSize: 12, marginTop: 12 }}>
            {this.state.error.stack}
          </pre>
        </div>
      );
    }
    return this.props.children;
  }
}

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </React.StrictMode>,
);
