import javax.swing.*;
import javax.swing.border.*;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.net.http.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.time.Duration;
import java.util.*;
import java.util.regex.*;

/**
 * Takoyo - Java edition.
 * A small desktop chat client that talks to any OpenAI-compatible endpoint
 * (the bundled Takoyo engine, OmniRoute, OpenAI, Ollama, ...).
 */
public class Takoyo {
    // ---- config ----
    static final Path CONFIG_DIR = Paths.get(System.getProperty("user.home"), ".takoyo-java");
    static final Path CONFIG_FILE = CONFIG_DIR.resolve("config.properties");
    static Properties config = new Properties();

    static String endpoint() { return config.getProperty("endpoint", "http://127.0.0.1:8765/v1/chat/completions"); }
    static String apiKey()   { return config.getProperty("apiKey", ""); }
    static String model()    { return config.getProperty("model", "qwen2.5-1.5b"); }

    static void loadConfig() {
        try { if (Files.exists(CONFIG_FILE)) { try (InputStream in = Files.newInputStream(CONFIG_FILE)) { config.load(in); } } }
        catch (Exception e) { /* ignore */ }
    }
    static void saveConfig() {
        try { Files.createDirectories(CONFIG_DIR); try (OutputStream out = Files.newOutputStream(CONFIG_FILE)) { config.store(out, "Takoyo Java config"); } }
        catch (Exception e) { /* ignore */ }
    }

    // ---- theme (dark, matching Takoyo) ----
    static final Color BG      = new Color(0x0B, 0x0B, 0x0D);
    static final Color INK     = new Color(0xFF, 0xFF, 0xFF);
    static final Color SOFT    = new Color(0xC9, 0xC9, 0xCE);
    static final Color MUTED   = new Color(0x8B, 0x8B, 0x8F);
    static final Color LINE    = new Color(0x26, 0x26, 0x2A);
    static final Color CHIP    = new Color(0x17, 0x17, 0x1B);
    static final Color ACTIVE  = new Color(0x24, 0x24, 0x2A);
    static final Color DANGER  = new Color(0xFF, 0x6B, 0x60);
    static final Color SUCCESS = new Color(0x2F, 0xBF, 0x71);
    static final Color CODEBLUE= new Color(0x79, 0xB8, 0xFF);

    static HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(10)).build();

    // ---- UI ----
    static JFrame frame;
    static JTextPane chat;
    static javax.swing.text.StyledDocument chatDoc;
    static JTextField input;
    static JButton sendBtn;
    static JLabel statusLabel;

    public static void main(String[] args) {
        loadConfig();
        SwingUtilities.invokeLater(Takoyo::buildUi);
    }

    static void buildUi() {
        frame = new JFrame("Takoyo (Java)");
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        frame.setSize(900, 700);
        frame.setMinimumSize(new Dimension(700, 500));

        JPanel root = new JPanel(new BorderLayout());
        root.setBackground(BG);
        frame.setContentPane(root);

        // header
        JPanel header = new JPanel(new BorderLayout());
        header.setBackground(BG);
        header.setBorder(BorderFactory.createEmptyBorder(12, 20, 12, 20));
        JLabel title = new JLabel("Takoyo");
        title.setFont(new Font("SansSerif", Font.BOLD, 20));
        title.setForeground(INK);
        header.add(title, BorderLayout.WEST);
        JButton settingsBtn = new JButton("⚙");
        styleButton(settingsBtn, false);
        settingsBtn.addActionListener(e -> openSettings());
        header.add(settingsBtn, BorderLayout.EAST);
        root.add(header, BorderLayout.NORTH);

        // chat
        chat = new JTextPane();
        chat.setEditable(false);
        chat.setBackground(BG);
        chat.setForeground(INK);
        chat.setFont(new Font("SansSerif", Font.PLAIN, 14));
        chat.setMargin(new Insets(12, 16, 12, 16));
        chatDoc = chat.getStyledDocument();
        JScrollPane scroll = new JScrollPane(chat);
        scroll.setBorder(null);
        scroll.getViewport().setBackground(BG);
        root.add(scroll, BorderLayout.CENTER);

        // input area
        JPanel south = new JPanel(new BorderLayout(8, 8));
        south.setBackground(BG);
        south.setBorder(BorderFactory.createEmptyBorder(10, 16, 14, 16));
        input = new JTextField();
        input.setBackground(CHIP);
        input.setForeground(INK);
        input.setCaretColor(INK);
        input.setFont(new Font("SansSerif", Font.PLAIN, 14));
        input.setBorder(BorderFactory.createCompoundBorder(
            BorderFactory.createLineBorder(LINE, 1),
            BorderFactory.createEmptyBorder(8, 12, 8, 12)));
        input.addActionListener(e -> sendMessage());
        south.add(input, BorderLayout.CENTER);
        sendBtn = new JButton("Send");
        styleButton(sendBtn, true);
        sendBtn.addActionListener(e -> sendMessage());
        south.add(sendBtn, BorderLayout.EAST);
        statusLabel = new JLabel("Ready");
        statusLabel.setForeground(MUTED);
        statusLabel.setFont(new Font("SansSerif", Font.PLAIN, 11));
        JPanel statusRow = new JPanel(new FlowLayout(FlowLayout.LEFT, 0, 0));
        statusRow.setBackground(BG);
        statusRow.add(statusLabel);
        south.add(statusRow, BorderLayout.SOUTH);
        root.add(south, BorderLayout.SOUTH);

        appendSystem("Takoyo (Java) is ready. Ask me to build something, and I'll write the code. "
                    + "Use the ⚙ button to set your API endpoint, key, and model.");

        frame.setLocationRelativeTo(null);
        frame.setVisible(true);
    }

    static void styleButton(JButton b, boolean primary) {
        b.setBackground(primary ? ACTIVE : CHIP);
        b.setForeground(INK);
        b.setFocusPainted(false);
        b.setBorder(BorderFactory.createCompoundBorder(
            BorderFactory.createLineBorder(LINE, 1),
            BorderFactory.createEmptyBorder(6, 14, 6, 14)));
        b.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
    }

    static void setStatus(String s) { SwingUtilities.invokeLater(() -> statusLabel.setText(s)); }

    static void appendSystem(String text) { appendMessage("Takoyo", text, SOFT); }
    static void appendUser(String text) { appendMessage("You", text, CODEBLUE); }

    static void appendMessage(String who, String text, Color color) {
        SwingUtilities.invokeLater(() -> {
            try {
                javax.swing.text.SimpleAttributeSet nameAttrs = new javax.swing.text.SimpleAttributeSet();
                javax.swing.text.StyleConstants.setForeground(nameAttrs, color);
                javax.swing.text.StyleConstants.setBold(nameAttrs, true);
                chatDoc.insertString(chatDoc.getLength(), who + ": ", nameAttrs);

                javax.swing.text.SimpleAttributeSet bodyAttrs = new javax.swing.text.SimpleAttributeSet();
                javax.swing.text.StyleConstants.setForeground(bodyAttrs, INK);
                // Render fenced code blocks with a Save button by splitting.
                renderTextWithCode(text, bodyAttrs);

                chatDoc.insertString(chatDoc.getLength(), "\n\n", bodyAttrs);
                chat.setCaretPosition(chatDoc.getLength());
            } catch (Exception e) { /* ignore */ }
        });
    }

    static void renderTextWithCode(String text, javax.swing.text.AttributeSet bodyAttrs) throws Exception {
        // Split on ``` fences
        Pattern fence = Pattern.compile("```([^\\n`]*)\\n(.*?)```", Pattern.DOTALL);
        Matcher m = fence.matcher(text);
        int pos = 0;
        while (m.find()) {
            String before = text.substring(pos, m.start());
            if (!before.trim().isEmpty()) {
                chatDoc.insertString(chatDoc.getLength(), before, bodyAttrs);
            }
            String info = m.group(1).trim();
            String code = m.group(2);
            // derive filename
            String filename = "";
            for (String tok : info.split("\\s+")) {
                if (tok.startsWith("file=")) { filename = tok.substring(5).replace("\"", "").replace("'", ""); }
            }
            if (filename.isEmpty()) {
                String lang = info.split("\\s+").length > 0 ? info.split("\\s+")[0].toLowerCase() : "";
                if (lang.isEmpty()) lang = "txt";
                filename = "code." + lang;
            }
            // language
            String lang = info.split("\\s+").length > 0 ? info.split("\\s+")[0] : "";
            if (lang.isEmpty()) lang = "code";

            // insert code in a monospace style
            javax.swing.text.SimpleAttributeSet codeAttrs = new javax.swing.text.SimpleAttributeSet();
            javax.swing.text.StyleConstants.setFontFamily(codeAttrs, "Consolas");
            javax.swing.text.StyleConstants.setFontSize(codeAttrs, 12);
            javax.swing.text.StyleConstants.setForeground(codeAttrs, SOFT);
            javax.swing.text.StyleConstants.setBackground(codeAttrs, CHIP);
            chatDoc.insertString(chatDoc.getLength(), "\n" + code.trim() + "\n", codeAttrs);

            // Save button
            javax.swing.text.SimpleAttributeSet btnAttrs = new javax.swing.text.SimpleAttributeSet();
            javax.swing.text.StyleConstants.setForeground(btnAttrs, CODEBLUE);
            chatDoc.insertString(chatDoc.getLength(), "  [Save code to file]\n", btnAttrs);
            // store a clickable action via a button in a separate panel instead
            addSaveButton(code, filename);

            pos = m.end();
        }
        String after = text.substring(pos);
        if (!after.trim().isEmpty()) {
            chatDoc.insertString(chatDoc.getLength(), after, bodyAttrs);
        }
    }

    static void addSaveButton(String code, String filename) {
        // Add a button row below chat via a popup approach: append to a panel.
        // Simpler: put button in south? Instead, add to chat as a component.
        JButton save = new JButton("Save " + filename);
        styleButton(save, false);
        save.addActionListener(e -> {
            JFileChooser fc = new JFileChooser();
            fc.setSelectedFile(new File(filename));
            if (fc.showSaveDialog(frame) == JFileChooser.APPROVE_OPTION) {
                try {
                    Files.writeString(fc.getSelectedFile().toPath(), code, StandardCharsets.UTF_8);
                    setStatus("Saved " + fc.getSelectedFile().getName());
                } catch (Exception ex) {
                    setStatus("Save failed: " + ex.getMessage());
                }
            }
        });
        JPanel row = new JPanel(new FlowLayout(FlowLayout.LEFT));
        row.setBackground(BG);
        row.add(save);
        // insert component into chat doc
        try {
            chat.insertComponent(row);
            chatDoc.insertString(chatDoc.getLength(), "\n", new javax.swing.text.SimpleAttributeSet());
        } catch (Exception e) { /* ignore */ }
    }

    static void sendMessage() {
        String text = input.getText().trim();
        if (text.isEmpty()) return;
        input.setText("");
        appendUser(text);
        setStatus("Thinking...");
        sendBtn.setEnabled(false);

        new Thread(() -> {
            try {
                String reply = callChat(text);
                appendMessage("Takoyo", reply, INK);
                // auto-save if a filename is mentioned and code present (best effort)
                autoSaveIfPossible(reply);
            } catch (Exception e) {
                appendMessage("Takoyo", "Error: " + e.getMessage(), DANGER);
            } finally {
                setStatus("Ready");
                SwingUtilities.invokeLater(() -> sendBtn.setEnabled(true));
            }
        }).start();
    }

    static void autoSaveIfPossible(String reply) {
        // If reply contains a fenced code block and mentions a filename, save it
        // automatically to the configured working folder (or home).
        Pattern fence = Pattern.compile("```[^\\n`]*\\n(.*?)```", Pattern.DOTALL);
        Matcher m = fence.matcher(reply);
        if (!m.find()) return;
        String code = m.group(1).trim();
        // find filename
        Matcher fm = Pattern.compile("\\b([A-Za-z][\\w-]*\\.(?:java|py|js|ts|cpp|c|h|cs|go|rs|rb|html|css|txt|json|md|sh|sql))\\b").matcher(reply);
        String filename = fm.find() ? fm.group(1) : "code.txt";
        String folder = config.getProperty("workingFolder", System.getProperty("user.home"));
        Path target = Paths.get(folder, filename);
        try {
            Files.createDirectories(target.getParent());
            Files.writeString(target, code, StandardCharsets.UTF_8);
            setStatus("Created " + target.toString());
            SwingUtilities.invokeLater(() -> appendSystem("Created file: " + target.toString()));
        } catch (Exception e) {
            setStatus("Auto-save failed: " + e.getMessage());
        }
    }

    static String callChat(String userText) throws Exception {
        // Build request JSON manually
        String json = "{\"model\":\"" + esc(model()) + "\",\"messages\":["
            + "{\"role\":\"system\",\"content\":\"" + esc("You are Takoyo, a helpful AI. When asked to build/create a file, output the complete code in a fenced block and also emit a write_file tool block. Never refuse to write code. Never tell the user to save it themselves.") + "\"},"
            + "{\"role\":\"user\",\"content\":\"" + esc(userText) + "\"}],\"stream\":false}";

        HttpRequest.Builder rb = HttpRequest.newBuilder()
            .uri(URI.create(endpoint()))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(json, StandardCharsets.UTF_8))
            .timeout(Duration.ofSeconds(120));
        String key = apiKey();
        if (key != null && !key.isEmpty()) rb.header("Authorization", "Bearer " + key);

        HttpResponse<String> resp = http.send(rb.build(), HttpResponse.BodyHandlers.ofString());
        if (resp.statusCode() >= 400) throw new RuntimeException("HTTP " + resp.statusCode() + ": " + resp.body());
        // extract content
        String body = resp.body();
        // find "content":"..."
        int idx = body.indexOf("\"content\"");
        if (idx < 0) return body;
        int colon = body.indexOf(':', idx);
        int q1 = body.indexOf('"', colon + 1);
        // handle escaped
        StringBuilder sb = new StringBuilder();
        boolean esc = false;
        for (int i = q1 + 1; i < body.length(); i++) {
            char c = body.charAt(i);
            if (esc) { sb.append(c); esc = false; continue; }
            if (c == '\\') { esc = true; sb.append(c); continue; }
            if (c == '"') break;
            sb.append(c);
        }
        String raw = sb.toString();
        // unescape
        return raw.replace("\\n", "\n").replace("\\t", "\t").replace("\\\"", "\"").replace("\\\\", "\\");
    }

    static String esc(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "").replace("\t", "\\t");
    }

    static void openSettings() {
        JTextField ep = new JTextField(endpoint());
        JTextField key = new JTextField(apiKey());
        JTextField mdl = new JTextField(model());
        JTextField folder = new JTextField(config.getProperty("workingFolder", System.getProperty("user.home")));
        JPanel p = new JPanel(new GridLayout(0, 1, 4, 4));
        p.add(new JLabel("Endpoint URL:")); p.add(ep);
        p.add(new JLabel("API key:")); p.add(key);
        p.add(new JLabel("Model:")); p.add(mdl);
        p.add(new JLabel("Working folder:")); p.add(folder);
        int r = JOptionPane.showConfirmDialog(frame, p, "Settings", JOptionPane.OK_CANCEL_OPTION);
        if (r == JOptionPane.OK_OPTION) {
            config.setProperty("endpoint", ep.getText().trim());
            config.setProperty("apiKey", key.getText().trim());
            config.setProperty("model", mdl.getText().trim());
            config.setProperty("workingFolder", folder.getText().trim());
            saveConfig();
            setStatus("Settings saved");
        }
    }
}
