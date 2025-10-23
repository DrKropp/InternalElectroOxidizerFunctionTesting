/*
 * WiFi Captive Portal CSS Styling
 * OrinTech ElectroOxidizer Device
 *
 * Modern, mobile-optimized styling for WiFiManager captive portal
 */

#ifndef PORTAL_CSS_H
#define PORTAL_CSS_H

// Custom CSS for WiFi provisioning portal - Modern mobile-optimized design
const char* PORTAL_CSS = R"(
<style>
@font-face {
  font-family: 'Inter';
  font-style: normal;
  font-weight: 400;
  src: url('/fonts/inter-400.woff2') format('woff2');
  font-display: swap;
}
@font-face {
  font-family: 'Inter';
  font-style: normal;
  font-weight: 500;
  src: url('/fonts/inter-500.woff2') format('woff2');
  font-display: swap;
}
@font-face {
  font-family: 'Inter';
  font-style: normal;
  font-weight: 600;
  src: url('/fonts/inter-600.woff2') format('woff2');
  font-display: swap;
}
@font-face {
  font-family: 'Inter';
  font-style: normal;
  font-weight: 700;
  src: url('/fonts/inter-700.woff2') format('woff2');
  font-display: swap;
}
body {
  font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  margin: 0;
  padding: 1rem;
  min-height: 100vh;
  -webkit-font-smoothing: antialiased;
}
.wrap {
  max-width: 500px;
  margin: 0 auto;
  background: #ffffff;
  border-radius: 1rem;
  box-shadow: 0 10px 25px rgba(0,0,0,0.2);
  padding: 2rem;
  margin-top: 2rem;
}
h1, h2, h3 {
  color: #4f46e5;
  font-weight: 700;
  letter-spacing: -0.02em;
  text-align: center;
  margin-top: 0;
}
h1 {
  font-size: clamp(1.5rem, 5vw, 2rem);
  margin-bottom: 0.5rem;
}
h3 {
  font-size: clamp(1rem, 3vw, 1.25rem);
  color: #64748b;
  font-weight: 500;
  margin-bottom: 1.5rem;
}
button, .button {
  background: linear-gradient(135deg, #6366f1, #4f46e5);
  color: white;
  border: none;
  border-radius: 0.75rem;
  padding: 1rem 2rem;
  font-size: 1rem;
  font-weight: 600;
  cursor: pointer;
  width: 100%;
  margin: 0.5rem 0;
  box-shadow: 0 4px 6px rgba(99,102,241,0.3);
  transition: all 0.2s ease;
  font-family: 'Inter', sans-serif;
  min-height: 48px;
  touch-action: manipulation;
}
button:hover, .button:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 12px rgba(99,102,241,0.4);
}
button:active, .button:active {
  transform: translateY(0);
}
.q {
  background: #f8fafc;
  border: 2px solid #e2e8f0;
  border-radius: 0.75rem;
  padding: 1rem;
  margin: 0.75rem 0;
  cursor: pointer;
  transition: all 0.2s ease;
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 48px;
}
.q:hover {
  border-color: #818cf8;
  background: #ffffff;
  transform: translateX(4px);
}
.q.l {
  font-weight: 600;
  color: #0f172a;
}
.q.r {
  color: #64748b;
  font-size: 0.875rem;
}
input[type="text"], input[type="password"], input[type="number"] {
  width: 100%;
  padding: 1rem;
  border: 2px solid #e2e8f0;
  border-radius: 0.75rem;
  font-size: 1rem;
  font-family: 'Inter', sans-serif;
  margin: 0.5rem 0;
  box-sizing: border-box;
  transition: all 0.2s ease;
  min-height: 48px;
}
input:focus {
  outline: none;
  border-color: #6366f1;
  box-shadow: 0 0 0 3px rgba(99,102,241,0.1);
}
label {
  display: block;
  color: #0f172a;
  font-weight: 600;
  margin-top: 1rem;
  margin-bottom: 0.5rem;
  font-size: 0.875rem;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.msg {
  background: #fef3c7;
  border-left: 4px solid #f59e0b;
  padding: 1rem;
  border-radius: 0.5rem;
  margin: 1rem 0;
  color: #92400e;
  font-size: 0.875rem;
}
.c {
  text-align: center;
  color: #64748b;
  font-size: 0.875rem;
  margin-top: 1.5rem;
  padding-top: 1.5rem;
  border-top: 2px solid #e2e8f0;
}
a {
  color: #6366f1;
  text-decoration: none;
  font-weight: 600;
}
a:hover {
  text-decoration: underline;
}
@media (max-width: 640px) {
  .wrap {
    padding: 1.5rem;
    margin-top: 1rem;
  }
  body {
    padding: 0.5rem;
  }
}
</style>
)";

#endif // PORTAL_CSS_H
