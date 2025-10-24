/*
 * WiFi Captive Portal CSS Styling
 * OrinTech ElectroOxidizer Device
 *
 * Modern, mobile-optimized styling for WiFiManager captive portal
 */

#ifndef PORTAL_CSS_H
#define PORTAL_CSS_H

// Custom CSS for WiFi provisioning portal - Colors and fonts only
const char* PORTAL_CSS = R"(
<style>
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: #1f2937;
}
.wrap {
  background: #ffffff;
}
h1, h2, h3 {
  color: #4f46e5;
}
h3 {
  color: #64748b;
}
button, .button {
  background: linear-gradient(135deg, #6366f1, #4f46e5);
  color: white;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
}
.q {
  background: #ffffff;
  border-color: #e2e8f0;
  color: #0f172a;
}
.q:hover {
  border-color: #818cf8;
  background: #f8fafc;
}
.q .l {
  color: #0f172a;
  font-weight: 600;
}
.q i {
  color: #64748b;
}
.q .q, .q .r {
  color: #64748b;
  font-weight: 500;
}
input[type="text"], input[type="password"], input[type="number"] {
  border-color: #e2e8f0;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  color: #1f2937;
}
input:focus {
  border-color: #6366f1;
  box-shadow: 0 0 0 3px rgba(99,102,241,0.1);
}
label {
  color: #0f172a;
  font-weight: 600;
}
.msg {
  background: #fef3c7;
  border-left-color: #f59e0b;
  color: #92400e;
}
.c {
  color: #64748b;
  border-top-color: #e2e8f0;
}
a {
  color: #6366f1;
  font-weight: 600;
}
</style>
)";

#endif // PORTAL_CSS_H
