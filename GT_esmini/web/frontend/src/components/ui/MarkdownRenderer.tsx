import Markdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import type { Components } from 'react-markdown';

interface MarkdownRendererProps {
  content: string;
  imageBaseUrl?: string;
  className?: string;
}

export function MarkdownRenderer({ content, imageBaseUrl, className = '' }: MarkdownRendererProps) {
  const components: Components = {
    h1: ({ children }) => (
      <h1 className="font-display text-2xl font-bold text-foreground mt-6 mb-3">{children}</h1>
    ),
    h2: ({ children }) => (
      <h2 className="font-display text-xl font-semibold text-foreground mt-5 mb-2">{children}</h2>
    ),
    h3: ({ children }) => (
      <h3 className="font-display text-lg font-medium text-foreground mt-4 mb-2">{children}</h3>
    ),
    p: ({ children }) => (
      <p className="text-text-secondary leading-relaxed mb-3">{children}</p>
    ),
    a: ({ href, children }) => (
      <a
        href={href}
        className="text-primary hover:text-accent-bright underline underline-offset-2 transition-colors"
        target="_blank"
        rel="noopener noreferrer"
      >
        {children}
      </a>
    ),
    img: ({ src, alt }) => {
      const resolvedSrc =
        src && imageBaseUrl && !src.startsWith('http')
          ? `${imageBaseUrl.replace(/\/$/, '')}/${src.replace(/^\//, '')}`
          : src;
      return (
        <img
          src={resolvedSrc}
          alt={alt ?? ''}
          className="max-w-full rounded border border-glass-edge my-3"
        />
      );
    },
    code: ({ children, className: codeClassName }) => {
      const isBlock = codeClassName?.startsWith('language-');
      if (isBlock) {
        return (
          <code className={`block font-mono text-sm bg-glass-1 p-3 rounded overflow-x-auto text-text-secondary my-3 ${codeClassName ?? ''}`}>
            {children}
          </code>
        );
      }
      return (
        <code className="font-mono text-sm bg-glass-1 px-1.5 py-0.5 rounded text-foreground">
          {children}
        </code>
      );
    },
    pre: ({ children }) => (
      <pre className="bg-glass-1 p-3 rounded overflow-x-auto my-3 border border-glass-edge">
        {children}
      </pre>
    ),
    ul: ({ children }) => (
      <ul className="list-disc list-inside text-text-secondary mb-3 space-y-1">{children}</ul>
    ),
    ol: ({ children }) => (
      <ol className="list-decimal list-inside text-text-secondary mb-3 space-y-1">{children}</ol>
    ),
    li: ({ children }) => (
      <li className="text-text-secondary">{children}</li>
    ),
    blockquote: ({ children }) => (
      <blockquote className="border-l-2 border-primary pl-4 my-3 text-text-secondary italic">
        {children}
      </blockquote>
    ),
    table: ({ children }) => (
      <div className="overflow-x-auto my-3">
        <table className="w-full text-sm border border-glass-edge">{children}</table>
      </div>
    ),
    thead: ({ children }) => (
      <thead className="border-b border-glass-edge bg-glass-1">{children}</thead>
    ),
    th: ({ children }) => (
      <th className="px-3 py-2 text-left font-medium text-foreground border-r border-glass-edge last:border-r-0">
        {children}
      </th>
    ),
    td: ({ children }) => (
      <td className="px-3 py-2 text-text-secondary border-r border-glass-edge last:border-r-0 border-b border-glass-edge/50">
        {children}
      </td>
    ),
    hr: () => <hr className="border-glass-edge my-4" />,
  };

  return (
    <div className={className}>
      <Markdown remarkPlugins={[remarkGfm]} components={components}>
        {content}
      </Markdown>
    </div>
  );
}
