import { Navigate, Route, Routes } from 'react-router-dom';
import { useAuth } from './auth.jsx';
import Login from './components/Login.jsx';
import ChatApp from './components/ChatApp.jsx';

function App() {
  const { user, loading } = useAuth();

  if (loading) {
    return (
      <div className="splash">
        <img src="/logo.svg" alt="" className="splash-logo" />
        <p className="splash-text">Ripple</p>
      </div>
    );
  }

  return (
    <Routes>
      <Route
        path="/login"
        element={user ? <Navigate to="/" replace /> : <Login mode="login" />}
      />
      <Route
        path="/register"
        element={user ? <Navigate to="/" replace /> : <Login mode="register" />}
      />
      <Route
        path="/"
        element={user ? <ChatApp /> : <Navigate to="/login" replace />}
      />
      <Route path="*" element={<Navigate to={user ? '/' : '/login'} replace />} />
    </Routes>
  );
}

export default App;
