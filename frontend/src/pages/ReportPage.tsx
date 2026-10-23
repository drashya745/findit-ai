import { Loader2, Send } from "lucide-react";
import { useCallback, useEffect, useId, useRef, useState, type FormEvent, type ReactNode } from "react";
import { Link } from "react-router-dom";
import { ErrorAlert, SuccessAlert } from "../components/States";
import { ApiError, api } from "../services/api";
import type { Category, CreatedReport, ReportType, User } from "../types/api";

const NEW_PERSON = "__new__";

type Field =
  | "reporter" | "newName" | "newContact" | "description" | "category" | "color" | "location" | "date"
  | "brand" | "heldAt";
type FieldErrors = Partial<Record<Field, string>>;

interface FormState {
  type: ReportType;
  reporterId: string;
  newName: string;
  newContact: string;
  description: string;
  category: string;
  color: string;
  location: string;
  date: string;
  brand: string;
  heldAt: string;
}

function today(): string {
  const now = new Date();
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;
}

const emptyForm = (type: ReportType, reporterId: string): FormState => ({
  type, reporterId, newName: "", newContact: "", description: "", category: "", color: "", location: "",
  date: today(), brand: "", heldAt: "",
});

/**
 * The server's validation messages start with the field they concern (e.g. "Description must be
 * at least 10 characters", "Unknown reporter ID: 'U999'"). This only decides *where* to show a
 * server message; the rules themselves live in the C++ domain.
 */
export function fieldForServerMessage(message: string, creatingUser: boolean): Field | null {
  const rules: Array<[RegExp, Field]> = [
    [/^(unknown reporter|invalid reporter)/i, "reporter"],
    [/^user name/i, creatingUser ? "newName" : "reporter"],
    [/^contact/i, "newContact"],
    [/^description/i, "description"],
    [/^category/i, "category"],
    [/^colou?r/i, "color"],
    [/^location/i, "location"],
    [/^date/i, "date"],
    [/^brand/i, "brand"],
    [/^held-at/i, "heldAt"],
  ];
  return rules.find(([pattern]) => pattern.test(message))?.[1] ?? null;
}

export function ReportPage() {
  const [users, setUsers] = useState<User[] | null>(null);
  const [categories, setCategories] = useState<Category[]>([]);
  const [loadError, setLoadError] = useState<unknown>(null);
  const [form, setForm] = useState<FormState>(emptyForm("lost", ""));
  const [errors, setErrors] = useState<FieldErrors>({});
  const [formError, setFormError] = useState<unknown>(null);
  const [submitting, setSubmitting] = useState(false);
  const [created, setCreated] = useState<CreatedReport | null>(null);
  const successRef = useRef<HTMLDivElement>(null);

  const loadOptions = useCallback(() => {
    setLoadError(null);
    Promise.all([api.users(), api.categories()]).then(
      ([u, c]) => {
        setUsers(u);
        setCategories(c);
      },
      setLoadError,
    );
  }, []);
  useEffect(loadOptions, [loadOptions]);

  useEffect(() => {
    if (created) successRef.current?.focus();
  }, [created]);

  const set = <K extends keyof FormState>(key: K, value: FormState[K]) => {
    setForm((f) => ({ ...f, [key]: value }));
    setErrors((e) => {
      const field: Field = key === "reporterId" ? "reporter" : (key as Field);
      if (!(field in e)) return e;
      const next = { ...e };
      delete next[field];
      return next;
    });
  };

  const creatingUser = form.reporterId === NEW_PERSON;

  const validate = (): FieldErrors => {
    const e: FieldErrors = {};
    const required = (value: string, field: Field, label: string) => {
      if (!value.trim()) e[field] = `${label} is required.`;
    };
    if (!form.reporterId) e.reporter = "Choose who is reporting this item.";
    if (creatingUser) required(form.newName, "newName", "Name");
    required(form.description, "description", "Description");
    required(form.category, "category", "Category");
    required(form.color, "color", "Colour");
    required(form.location, "location", "Location");
    required(form.date, "date", "Date");
    return e;
  };

  const onSubmit = async (event: FormEvent) => {
    event.preventDefault();
    if (submitting) return;
    setCreated(null);
    setFormError(null);
    const clientErrors = validate();
    setErrors(clientErrors);
    if (Object.keys(clientErrors).length > 0) return;

    setSubmitting(true);
    try {
      let reporterId = form.reporterId;
      if (creatingUser) {
        const user = await api.createUser({ name: form.newName, contact: form.newContact || undefined });
        setUsers((u) => [...(u ?? []), { id: user.id, name: user.name }]);
        reporterId = user.id;
        setForm((f) => ({ ...f, reporterId: user.id, newName: "", newContact: "" }));
      }
      const report = await api.createReport({
        type: form.type,
        reporterId,
        category: form.category,
        description: form.description,
        color: form.color,
        location: form.location,
        date: form.date,
        brand: form.brand || undefined,
        heldAt: form.type === "found" ? form.heldAt || undefined : undefined,
      });
      setCreated(report); // Shown only after the API accepted (and saved) the report.
      setForm(emptyForm(form.type, reporterId));
      setErrors({});
    } catch (error) {
      const field = error instanceof ApiError && error.status === 400 ? fieldForServerMessage(error.message, creatingUser) : null;
      if (field) {
        setErrors({ [field]: error instanceof Error ? error.message : String(error) });
      } else {
        setFormError(error);
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="max-w-3xl space-y-6">
      <header>
        <h1 className="page-title">Report an item</h1>
        <p className="page-subtitle">Describe the item as precisely as you can; the description drives text matching.</p>
      </header>

      {created && (
        <div ref={successRef} tabIndex={-1} className="outline-none">
          <SuccessAlert title={`Report ${created.id} saved`}>
            {created.persisted ? "It has been stored and is now searchable." : "Sample mode: it exists only until the server stops."}{" "}
            <Link className="font-medium underline" to={`/browse?q=${encodeURIComponent(created.id)}`}>
              View it
            </Link>
            {created.type === "lost" && (
              <>
                {" · "}
                <Link className="font-medium underline" to={`/matches?lost=${encodeURIComponent(created.id)}`}>
                  Find matches
                </Link>
              </>
            )}
          </SuccessAlert>
        </div>
      )}
      {loadError != null && <ErrorAlert error={loadError} onRetry={loadOptions} title="Could not load reporters and categories" />}
      {formError != null && <ErrorAlert error={formError} title="Report not saved" />}

      <form noValidate onSubmit={onSubmit} className="card space-y-5 p-5" aria-label="Report an item">
        <fieldset>
          <legend className="label">Report type</legend>
          <div className="inline-flex rounded-md border border-slate-300 p-0.5" role="radiogroup" aria-label="Report type">
            {(["lost", "found"] as const).map((type) => (
              <label
                key={type}
                className={`cursor-pointer rounded px-4 py-1.5 text-sm font-medium focus-within:ring-2 focus-within:ring-blue-600 ${
                  form.type === type ? "bg-blue-600 text-white" : "text-slate-700 hover:bg-slate-100"
                }`}
              >
                <input
                  type="radio"
                  name="type"
                  value={type}
                  checked={form.type === type}
                  onChange={() => set("type", type)}
                  className="sr-only"
                />
                {type === "lost" ? "I lost something" : "I found something"}
              </label>
            ))}
          </div>
        </fieldset>

        <div className="grid gap-4 sm:grid-cols-2">
          <FormField label="Reporter" error={errors.reporter} required>
            {(props) => (
              <select
                {...props}
                className="input"
                value={form.reporterId}
                onChange={(e) => set("reporterId", e.target.value)}
                disabled={users === null}
              >
                <option value="">{users === null ? "Loading…" : "Select a person"}</option>
                {users?.map((u) => (
                  <option key={u.id} value={u.id}>
                    {u.name} ({u.id})
                  </option>
                ))}
                <option value={NEW_PERSON}>+ Register a new person</option>
              </select>
            )}
          </FormField>
          <FormField label="Date" error={errors.date} required hint={form.type === "lost" ? "When it was lost" : "When it was found"}>
            {(props) => <input {...props} type="date" className="input" value={form.date} onChange={(e) => set("date", e.target.value)} />}
          </FormField>
        </div>

        {creatingUser && (
          <div className="grid gap-4 rounded-md border border-slate-200 bg-slate-50 p-4 sm:grid-cols-2">
            <FormField label="New person's name" error={errors.newName} required>
              {(props) => <input {...props} className="input" value={form.newName} onChange={(e) => set("newName", e.target.value)} autoComplete="name" />}
            </FormField>
            <FormField label="Contact (optional)" error={errors.newContact} hint="Email or phone; not shown to others">
              {(props) => <input {...props} className="input" value={form.newContact} onChange={(e) => set("newContact", e.target.value)} />}
            </FormField>
          </div>
        )}

        <FormField label="Description" error={errors.description} required hint="E.g. “Black leather wallet with a student ID card”">
          {(props) => (
            <textarea {...props} rows={3} className="input" value={form.description} onChange={(e) => set("description", e.target.value)} />
          )}
        </FormField>

        <div className="grid gap-4 sm:grid-cols-3">
          <FormField label="Category" error={errors.category} required>
            {(props) => (
              <>
                <input {...props} className="input" list="category-options" value={form.category} onChange={(e) => set("category", e.target.value)} />
                <datalist id="category-options">
                  {categories.map((c) => (
                    <option key={c.name} value={c.name} />
                  ))}
                </datalist>
              </>
            )}
          </FormField>
          <FormField label="Colour" error={errors.color} required>
            {(props) => <input {...props} className="input" value={form.color} onChange={(e) => set("color", e.target.value)} />}
          </FormField>
          <FormField label="Location" error={errors.location} required>
            {(props) => <input {...props} className="input" value={form.location} onChange={(e) => set("location", e.target.value)} />}
          </FormField>
        </div>

        <div className="grid gap-4 sm:grid-cols-2">
          <FormField label="Brand (optional)" error={errors.brand}>
            {(props) => <input {...props} className="input" value={form.brand} onChange={(e) => set("brand", e.target.value)} />}
          </FormField>
          {form.type === "found" && (
            <FormField label="Where is it held now? (optional)" error={errors.heldAt}>
              {(props) => <input {...props} className="input" value={form.heldAt} onChange={(e) => set("heldAt", e.target.value)} />}
            </FormField>
          )}
        </div>

        <div className="flex items-center justify-end gap-3 border-t border-slate-100 pt-4">
          <button type="submit" className="btn-primary" disabled={submitting}>
            {submitting ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Send className="h-4 w-4" aria-hidden="true" />}
            {submitting ? "Saving…" : `Submit ${form.type} report`}
          </button>
        </div>
      </form>
    </div>
  );
}

interface ControlProps {
  id: string;
  "aria-invalid": boolean;
  "aria-describedby"?: string;
  "aria-required"?: boolean;
}

function FormField({ label, error, hint, required, children }: {
  label: string;
  error?: string;
  hint?: string;
  required?: boolean;
  children: (props: ControlProps) => ReactNode;
}) {
  const id = useId();
  const hintId = hint ? `${id}-hint` : undefined;
  const errorId = error ? `${id}-error` : undefined;
  const describedBy = [errorId, hintId].filter(Boolean).join(" ") || undefined;
  return (
    <div>
      <label htmlFor={id} className="label">
        {label}
        {required && <span className="text-red-600" aria-hidden="true"> *</span>}
      </label>
      {children({ id, "aria-invalid": Boolean(error), "aria-describedby": describedBy, "aria-required": required || undefined })}
      {hint && !error && <p id={hintId} className="mt-1 text-xs text-slate-500">{hint}</p>}
      {error && <p id={errorId} className="field-error">{error}</p>}
    </div>
  );
}
