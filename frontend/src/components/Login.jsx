import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { useAuth } from '../auth.jsx';

export default function Login({ mode }) {
  const isRegister = mode === 'register';
  const { login, register } = useAuth();
  const navigate = useNavigate();

  const [username, setUsername] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  const handleSubmit = async (e) => {
    e.preventDefault();
    setError('');
    setSubmitting(true);
    try {
      if (isRegister) {
        await register(username.trim(), displayName.trim(), password);
      } else {
        await login(username.trim(), password);
      }
      navigate('/');
    } catch (err) {
      setError(err.message || 'Something went wrong');
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="auth-page">
      <div className="auth-hero">
        <img src="/logo.svg" alt="" className="auth-logo" />
        <h1 className="auth-brand">Ripple</h1>
        <p className="auth-tagline">Messages that ripple.</p>
        <p className="auth-headline">
          {isRegister ? 'Create your account' : 'Welcome back'}
        </p>
        <p className="auth-subline">
          {isRegister
            ? 'Join the conversation — realtime, private, and effortless.'
            : 'Pick up where you left off.'}
        </p>
      </div>

      <form className="auth-form" onSubmit={handleSubmit}>
        {error && <div className="auth-error">{error}</div>}

        <label className="field">
          <span>Username</span>
          <input
            type="text"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            autoComplete="username"
            required
            minLength={3}
            placeholder="yourname"
          />
        </label>

        {isRegister && (
          <label className="field">
            <span>Display name</span>
            <input
              type="text"
              value={displayName}
              onChange={(e) => setDisplayName(e.target.value)}
              autoComplete="name"
              required
              minLength={1}
              placeholder="Your Name"
            />
          </label>
        )}

        <label className="field">
          <span>Password</span>
          <input
            type="password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            autoComplete={isRegister ? 'new-password' : 'current-password'}
            required
            minLength={6}
            placeholder="••••••••"
          />
        </label>

        <button type="submit" className="btn btn-primary btn-full" disabled={submitting}>
          {submitting ? 'Please wait…' : isRegister ? 'Create account' : 'Sign in'}
        </button>

        <p className="auth-switch">
          {isRegister ? (
            <>
              Already have an account?{' '}
              <Link to="/login">Sign in</Link>
            </>
          ) : (
            <>
              New here?{' '}
              <Link to="/register">Create account</Link>
            </>
          )}
        </p>
      </form>
    </div>
  );
}
